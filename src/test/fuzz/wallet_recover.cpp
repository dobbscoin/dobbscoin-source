// Copyright (c) 2026 The Dobbscoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// wallet_recover: arbitrary bytes as a damaged wallet.dat (Berkeley DB or
// SQLite), then CWalletDB::Recover(fOnlyKeys=true) -- what -salvagewallet
// does. It runs BerkeleyRO::Salvage or the SQLite reader, ReadKeyValue on
// every record, and CDBEnv::ReplaceWithSQLite.
//
// Invariants:
//  - Failure: wallet.dat is byte-identical to the input and nothing else is
//    left in the directory.
//  - Success: the directory holds exactly wallet.dat and
//    wallet.dat.<bdb|salvage>-<MOCK_TIME> ("bdb" for a Berkeley DB input);
//    that backup is byte-identical to the input; the new wallet.dat is an
//    SQLite wallet holding at least one record, every one of them a key
//    record ("key", "wkey", "mkey", "ckey") that the salvage read returned.
//  - Known, reported: stray wallet.dat-shm/-wal after opening a WAL-mode
//    file are tolerated unless BOB_FUZZ_STRICT_FILES=1 (see ListChecked).

#include "fuzz_util.h"

#include "bdbro.h"
#include "db.h"
#include "serialize.h"
#include "streams.h"
#include "tinyformat.h"
#include "walletdb.h"

#include <algorithm>
#include <stdlib.h>

#include <boost/filesystem.hpp>

namespace fs = boost::filesystem;

namespace
{
fs::path g_dir;
fs::path g_file;
CDBEnv* g_env;

bool IsKeyRecord(const fuzz::Bytes& key)
{
    CDataStream ss(key, SER_DISK, CLIENT_VERSION);
    std::string strType;
    try {
        ss >> strType;
    } catch (const std::exception&) {
        return false;
    }
    return strType == "key" || strType == "wkey" || strType == "mkey" || strType == "ckey";
}
/**
 * Known and reported: a file whose header says WAL mode makes SQLite create
 * wallet.dat-shm and wallet.dat-wal when Recover opens it, even read-only,
 * and they stay behind. Unless BOB_FUZZ_STRICT_FILES=1, those two names are
 * left out of the directory checks so that one finding does not stop every run.
 */
std::vector<std::string> ListChecked(const fs::path& dir)
{
    std::vector<std::string> files = fuzz::List(dir);
    const char* env = getenv("BOB_FUZZ_STRICT_FILES");
    if (env && std::string(env) == "1")
        return files;
    std::vector<std::string> out;
    for (size_t i = 0; i < files.size(); i++)
        if (files[i] != "wallet.dat-shm" && files[i] != "wallet.dat-wal")
            out.push_back(files[i]);
    return out;
}
} // namespace

extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv)
{
    g_dir = fuzz::Setup("wallet_recover");
    g_file = g_dir / "wallet.dat";
    g_env = new CDBEnv();
    if (!g_env->Open(g_dir))
        abort();
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    fuzz::Clear(g_dir);
    fuzz::WriteFile(g_file, data, size);
    const fuzz::Bytes input(data, data + size);
    const bool fBerkeley = BerkeleyRO::IsBerkeleyBtreeFile(g_file);

    // What the salvage read can return, from a copy so the real directory
    // sees nothing but Recover.
    fuzz::Records candidates;
    {
        const fs::path side = fuzz::SideDir();
        fuzz::Clear(side);
        fuzz::WriteFile(side / "wallet.dat", data, size);
        std::string strIgnored;
        if (fBerkeley)
            BerkeleyRO::Salvage(side / "wallet.dat", candidates, strIgnored);
        else
            ReadSQLiteWalletFile(side / "wallet.dat", candidates, strIgnored);
        fuzz::Clear(side);
        std::sort(candidates.begin(), candidates.end());
    }

    std::string strError;
    const bool fOk = CWalletDB::Recover(*g_env, "wallet.dat", true, strError);
    const std::vector<std::string> files = ListChecked(g_dir);
    FUZZ_CHECK(g_env->mapDb.empty(), "Recover left a database open");

    if (!fOk) {
        FUZZ_CHECK(!strError.empty(), "Recover failed without saying why");
        FUZZ_CHECK(fuzz::ReadFile(g_file) == input, "a failed Recover modified wallet.dat: " + strError);
        FUZZ_CHECK(files == std::vector<std::string>(1, "wallet.dat"),
                   "a failed Recover left files behind: " + fuzz::Join(files) + " (" + strError + ")");
        return 0;
    }

    const std::string strBackup = strprintf("wallet.dat.%s-%d", fBerkeley ? "bdb" : "salvage", fuzz::MOCK_TIME);
    std::vector<std::string> vExpectedFiles;
    vExpectedFiles.push_back("wallet.dat");
    vExpectedFiles.push_back(strBackup);
    FUZZ_CHECK(files == vExpectedFiles, "after Recover the directory holds: " + fuzz::Join(files));
    FUZZ_CHECK(fuzz::ReadFile(g_dir / strBackup) == input, "the preserved original differs from the input");

    fuzz::Records got;
    std::string strReadBack;
    FUZZ_CHECK(ReadSQLiteWalletFile(g_file, got, strReadBack), "the recovered wallet cannot be read: " + strReadBack);
    FUZZ_CHECK(!got.empty(), "Recover succeeded with an empty wallet");
    for (size_t i = 0; i < got.size(); i++) {
        FUZZ_CHECK(IsKeyRecord(got[i].first), strprintf("recovered record %u is not a key record", (unsigned int)i));
        FUZZ_CHECK(std::binary_search(candidates.begin(), candidates.end(), got[i]),
                   strprintf("recovered record %u was not in what the salvage read returned", (unsigned int)i));
    }
    return 0;
}
