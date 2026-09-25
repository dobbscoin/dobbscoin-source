// Copyright (c) 2026 The Dobbscoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// wallet_migrate: arbitrary bytes as wallet.dat in an otherwise empty
// datadir, then CDBEnv::MigrateFromBerkeley -- what the first start of
// v0.14.0 does to every user's old wallet.
//
// Invariants:
//  - Not a Berkeley DB file (no btree magic): returns true and touches
//    nothing; wallet.dat is byte-identical and alone in the directory.
//  - Failure: strError is set, wallet.dat is byte-identical to the input and
//    nothing else is left in the directory. Failure only happens when
//    BerkeleyRO::ReadAll rejects the file (on tmpfs nothing else can fail).
//  - Success: ReadAll accepted the file; the directory holds exactly
//    wallet.dat and wallet.dat.bdb-<MOCK_TIME>; that backup is byte-identical
//    to the input; wallet.dat is now an SQLite wallet whose records are
//    exactly what ReadAll read.

#include "fuzz_util.h"

#include "bdbro.h"
#include "db.h"
#include "tinyformat.h"

#include <boost/filesystem.hpp>

namespace fs = boost::filesystem;

namespace
{
fs::path g_dir;
fs::path g_file;
CDBEnv* g_env;
} // namespace

extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv)
{
    g_dir = fuzz::Setup("wallet_migrate");
    g_file = g_dir / "wallet.dat";
    g_env = new CDBEnv(); // our own environment, not the global bitdb
    if (!g_env->Open(g_dir))
        abort();
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    fuzz::Clear(g_dir);
    fuzz::WriteFile(g_file, data, size);
    const fuzz::Bytes input(data, data + size);
    const std::vector<std::string> vOnlyWallet(1, "wallet.dat");

    const bool fBerkeley = BerkeleyRO::IsBerkeleyBtreeFile(g_file);
    BerkeleyRO::RecordMap expected;
    bool fReadable = false;
    std::string strReadError;
    try {
        BerkeleyRO::ReadAll(g_file, expected);
        fReadable = true;
    } catch (const std::runtime_error& e) {
        strReadError = e.what();
    }

    std::string strError;
    const bool fOk = g_env->MigrateFromBerkeley("wallet.dat", strError);
    const std::vector<std::string> files = fuzz::List(g_dir);
    FUZZ_CHECK(g_env->mapDb.empty(), "the migration left a database open");

    if (!fBerkeley) {
        FUZZ_CHECK(fOk, "a non-Berkeley file made the migration fail: " + strError);
        FUZZ_CHECK(files == vOnlyWallet, "a non-Berkeley file was touched; directory: " + fuzz::Join(files));
        FUZZ_CHECK(fuzz::ReadFile(g_file) == input, "a non-Berkeley wallet.dat was modified");
        return 0;
    }

    if (!fOk) {
        FUZZ_CHECK(!strError.empty(), "the migration failed without saying why");
        FUZZ_CHECK(fuzz::ReadFile(g_file) == input, "a failed migration modified wallet.dat: " + strError);
        FUZZ_CHECK(files == vOnlyWallet, "a failed migration left files behind: " + fuzz::Join(files) + " (" + strError + ")");
        FUZZ_CHECK(!fReadable, "ReadAll reads the file but the migration failed: " + strError);
        return 0;
    }

    FUZZ_CHECK(fReadable, "the migration succeeded on a file ReadAll rejects: " + strReadError);
    const std::string strBackup = strprintf("wallet.dat.bdb-%d", fuzz::MOCK_TIME);
    std::vector<std::string> vExpectedFiles;
    vExpectedFiles.push_back("wallet.dat");
    vExpectedFiles.push_back(strBackup);
    FUZZ_CHECK(files == vExpectedFiles, "after a migration the directory holds: " + fuzz::Join(files));
    FUZZ_CHECK(fuzz::ReadFile(g_dir / strBackup) == input, "the preserved original differs from the input");
    FUZZ_CHECK(!BerkeleyRO::IsBerkeleyBtreeFile(g_file), "wallet.dat is still a Berkeley DB file after migrating");

    fuzz::Records got;
    std::string strReadBack;
    FUZZ_CHECK(ReadSQLiteWalletFile(g_file, got, strReadBack), "the migrated wallet cannot be read: " + strReadBack);
    FUZZ_CHECK(got == fuzz::Records(expected.begin(), expected.end()),
               strprintf("the migrated wallet holds %u records, ReadAll read %u (or their bytes differ)",
                         (unsigned int)got.size(), (unsigned int)expected.size()));
    FUZZ_CHECK(fuzz::List(g_dir) == vExpectedFiles, "reading the migrated wallet left files behind");
    return 0;
}
