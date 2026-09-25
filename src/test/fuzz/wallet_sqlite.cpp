// Copyright (c) 2026 The Dobbscoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// wallet_sqlite: arbitrary bytes as an SQLite wallet.dat, opened the way the
// node opens it at startup: CDBEnv::Verify (integrity_check), then a CDB handle
// that reads the version record and walks every record with a cursor, plus
// the standalone reader ReadSQLiteWalletFile that -salvagewallet and the
// migration use. Every one of them must reject or read, never crash.
//
// Invariants:
//  - No crash, no sanitizer report, no exception other than the
//    std::runtime_error CDB documents for a file it cannot open.
//  - A Berkeley DB file is refused by Verify and by CDB.
//  - When Verify says VERIFY_OK (the file passes PRAGMA integrity_check, the
//    node's gate at startup): the cursor returns keys in strictly increasing
//    order (so it always ends), every key the cursor returns is found again by
//    a point lookup, and if both readers succeed, the cursor walk and
//    ReadSQLiteWalletFile return exactly the same records. A file Verify
//    rejects only has to be handled without a crash.
//  - This harness found that Verify once ran quick_check, which passed a file
//    whose key index disagreed with its table; CDB then silently skipped a
//    record. Verify now runs integrity_check, and the checks above apply to
//    every file it accepts.

#include "fuzz_util.h"

#include "bdbro.h"
#include "db.h"
#include "serialize.h"
#include "tinyformat.h"
#include "util.h"

#include <stdlib.h>

#include <boost/filesystem.hpp>

#include <sqlite3.h>

namespace fs = boost::filesystem;

namespace
{
fs::path g_dir;
fs::path g_file;
const char* const WALLET = "wallet.dat";

/** CDB's protected interface, read-only. */
class FuzzReader : public CDB
{
public:
    explicit FuzzReader(const std::string& strFile) : CDB(strFile, "r") {}

    /**
     * 1: walked to the end, 0: cursor error (records so far in out).
     * A cursor that fails to move forward is a bug on a verified file; on
     * any other file the walk just stops there.
     */
    int Walk(fuzz::Records& out, bool fVerified)
    {
        out.clear();
        CDBCursor* pcursor = GetCursor();
        if (!pcursor)
            return 0;
        int ret;
        while (true) {
            CDataStream ssKey(SER_DISK, CLIENT_VERSION), ssValue(SER_DISK, CLIENT_VERSION);
            ret = ReadAtCursor(pcursor, ssKey, ssValue);
            if (ret != DB_CURSOR_OK)
                break;
            fuzz::Bytes key(ssKey.begin(), ssKey.end()), value(ssValue.begin(), ssValue.end());
            if (!out.empty() && !(out.back().first < key)) {
                FUZZ_CHECK(!fVerified, "the cursor did not move forward");
                ret = DB_CURSOR_ERROR;
                break;
            }
            out.push_back(std::make_pair(key, value));
        }
        CloseCursor(pcursor);
        return ret == DB_CURSOR_DONE ? 1 : 0;
    }

    bool Has(fuzz::Bytes key)
    {
        return Exists(CFlatData(key));
    }

    void ReadSome()
    {
        int nVersion = 0;
        ReadVersion(nVersion);
        int nMinVersion = 0;
        Read(std::string("minversion"), nMinVersion);
        std::string strName;
        Read(std::make_pair(std::string("name"), std::string("")), strName);
    }
};

void Reset()
{
    LOCK(bitdb.cs_db);
    bitdb.CloseDb(WALLET);
    bitdb.mapFileUseCount.erase(WALLET);
}
} // namespace

extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv)
{
    g_dir = fuzz::Setup("wallet_sqlite");
    g_file = g_dir / WALLET;
    if (!bitdb.Open(g_dir))
        abort();
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    Reset();
    fuzz::Clear(g_dir);
    fuzz::WriteFile(g_file, data, size);
    const bool fBerkeley = BerkeleyRO::IsBerkeleyBtreeFile(g_file);

    std::string strError;
    CDBEnv::VerifyResult verify = bitdb.Verify(WALLET, strError);
    const bool fVerified = verify == CDBEnv::VERIFY_OK;
    FUZZ_CHECK(verify == CDBEnv::VERIFY_OK || verify == CDBEnv::RECOVER_FAIL, "Verify returned an unexpected value");
    if (verify != CDBEnv::VERIFY_OK)
        FUZZ_CHECK(!strError.empty(), "Verify failed without saying why");
    if (fBerkeley)
        FUZZ_CHECK(verify != CDBEnv::VERIFY_OK, "Verify accepted a Berkeley DB file");

    fuzz::Records viaFile;
    std::string strFileError;
    const bool fFile = ReadSQLiteWalletFile(g_file, viaFile, strFileError);
    if (fFile)
        FUZZ_CHECK(!fBerkeley, "ReadSQLiteWalletFile read a Berkeley DB file");

    fuzz::Records viaCursor;
    int nWalk = -1; // -1: CDB refused to open the file
    try {
        FuzzReader db(WALLET);
        FUZZ_CHECK(!fBerkeley, "CDB opened a Berkeley DB file");
        db.ReadSome();
        nWalk = db.Walk(viaCursor, fVerified);
        for (size_t i = 0; i < viaCursor.size(); i++) {
            // On a file Verify rejects the node never gets this far, and a
            // damaged index can legitimately disagree with the table.
            bool fHas = db.Has(viaCursor[i].first);
            if (fVerified)
                FUZZ_CHECK(fHas, strprintf("cursor key %u is not found by a lookup", (unsigned int)i));
        }
    } catch (const std::runtime_error&) {
    }
    Reset();

    if (fVerified && fFile && nWalk == 1)
        FUZZ_CHECK(viaCursor == viaFile, strprintf("the cursor read %u records, ReadSQLiteWalletFile %u (or their bytes differ)",
                                                   (unsigned int)viaCursor.size(), (unsigned int)viaFile.size()));
    return 0;
}
