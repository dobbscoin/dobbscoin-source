// Copyright (c) 2026 The Dobbscoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// fuzz_seedgen: writes the seed corpora in src/test/fuzz/corpus/ from the
// checked-in Berkeley DB fixtures. Run from the top of the source tree:
//
//   build-fuzz/src/test/fuzz/fuzz_seedgen . src/test/fuzz/corpus
//
// It is deterministic, so rerunning it rewrites the same files (except that
// SQLite files carry no timestamps and come out byte-identical too).

#include "fuzz_util.h"

#include "bdbro.h"
#include "db.h"
#include "tinyformat.h"

#include <set>
#include <stdio.h>

#include <boost/filesystem.hpp>

#include <sqlite3.h>

namespace fs = boost::filesystem;

namespace
{
struct Fixture {
    const char* path;  // relative to the source tree
    const char* name;  // seed file stem
};
const Fixture FIXTURES[] = {
    {"qa/rpc-tests/data/wallet-v0.13.8-bdb-plain.dat", "v0.13.8-plain"},
    {"qa/rpc-tests/data/wallet-v0.13.8-bdb-encrypted.dat", "v0.13.8-encrypted"},
    {"src/test/data/bdb_fixture.raw", "bdb-fixture-512"},
};

void Copy(const fs::path& from, const fs::path& to)
{
    fuzz::Bytes b = fuzz::ReadFile(from);
    fuzz::WriteFile(to, b.data(), b.size());
}

/** FuzzedDataProvider::ConsumeRandomLengthString's framing. */
void AppendString(std::string& out, const fuzz::Bytes& s, bool fLast)
{
    for (size_t i = 0; i < s.size(); i++) {
        out += (char)s[i];
        if (s[i] == '\\')
            out += '\\';
    }
    if (!fLast)
        out += "\\x";
}

void WriteLoadSeed(const fs::path& p, const fuzz::Records& records)
{
    std::string s;
    for (size_t i = 0; i < records.size(); i++) {
        AppendString(s, records[i].first, false);
        AppendString(s, records[i].second, i + 1 == records.size());
    }
    fuzz::WriteFile(p, (const uint8_t*)s.data(), s.size());
}

/** An SQLite wallet with small pages, built by hand (CreateSQLiteWalletFile uses 4096). */
bool WriteSmallPageWallet(const fs::path& p, const fuzz::Records& records)
{
    fs::remove(p);
    sqlite3* db = NULL;
    if (sqlite3_open(p.string().c_str(), &db) != SQLITE_OK)
        return false;
    std::string sql = strprintf("PRAGMA page_size=512; PRAGMA journal_mode=DELETE; BEGIN; "
                                "CREATE TABLE main(key BLOB PRIMARY KEY NOT NULL, value BLOB NOT NULL); "
                                "PRAGMA application_id=%d; PRAGMA user_version=1; COMMIT;",
                                0x424f4257);
    bool fOk = sqlite3_exec(db, sql.c_str(), NULL, NULL, NULL) == SQLITE_OK;
    sqlite3_stmt* stmt = NULL;
    fOk = fOk && sqlite3_prepare_v2(db, "INSERT INTO main VALUES (?, ?)", -1, &stmt, NULL) == SQLITE_OK;
    for (size_t i = 0; fOk && i < records.size(); i++) {
        sqlite3_bind_blob(stmt, 1, records[i].first.data(), records[i].first.size(), SQLITE_TRANSIENT);
        sqlite3_bind_blob(stmt, 2, records[i].second.data(), records[i].second.size(), SQLITE_TRANSIENT);
        fOk = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_reset(stmt);
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return fOk;
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s <source tree> <corpus dir>\n", argv[0]);
        return 1;
    }
    fs::path src(argv[1]), out(argv[2]);
    fuzz::Setup("seedgen");
    sqlite3_initialize();
    const char* targets[] = {"bdbro_parse", "bdbro_salvage", "wallet_migrate", "wallet_sqlite", "wallet_recover", "wallet_load"};
    for (size_t i = 0; i < sizeof(targets) / sizeof(targets[0]); i++)
        fs::create_directories(out / targets[i]);

    fuzz::Records small; // a few records from the plain wallet, for the smallest seeds
    for (size_t i = 0; i < sizeof(FIXTURES) / sizeof(FIXTURES[0]); i++) {
        const Fixture& fx = FIXTURES[i];
        BerkeleyRO::RecordMap m;
        BerkeleyRO::ReadAll(src / fx.path, m);
        fuzz::Records records(m.begin(), m.end());
        printf("%s: %u records\n", fx.path, (unsigned int)records.size());
        std::string bdb = strprintf("%s.bdb", fx.name);
        Copy(src / fx.path, out / "bdbro_parse" / bdb);
        Copy(src / fx.path, out / "bdbro_salvage" / bdb);
        Copy(src / fx.path, out / "wallet_migrate" / bdb);
        Copy(src / fx.path, out / "wallet_recover" / bdb);
        if (i == 2)
            continue; // the synthetic fixture holds no wallet records

        std::string strError;
        fs::path sqlite = out / "wallet_sqlite" / strprintf("%s.sqlite", fx.name);
        if (!CreateSQLiteWalletFile(sqlite, records, strError)) {
            fprintf(stderr, "%s\n", strError.c_str());
            return 1;
        }
        Copy(sqlite, out / "wallet_recover" / strprintf("%s.sqlite", fx.name));
        WriteLoadSeed(out / "wallet_load" / strprintf("%s.records", fx.name), records);
        if (i == 0) {
            // One record of each type. A key starts with its type name,
            // serialized with a one-byte length.
            std::set<std::string> types;
            for (size_t j = 0; j < records.size(); j++) {
                const fuzz::Bytes& k = records[j].first;
                size_t n = k.empty() ? 0 : std::min<size_t>(k[0], k.size() - 1);
                std::string type(k.begin() + (k.empty() ? 0 : 1), k.begin() + (k.empty() ? 0 : 1) + n);
                if (types.insert(type).second)
                    small.push_back(records[j]);
            }
        }
    }
    printf("one record per type: %u records\n", (unsigned int)small.size());
    std::string strError;
    if (!CreateSQLiteWalletFile(out / "wallet_sqlite" / "empty.sqlite", fuzz::Records(), strError) ||
        !WriteSmallPageWallet(out / "wallet_sqlite" / "types-512.sqlite", small)) {
        fprintf(stderr, "cannot write the small SQLite seeds %s\n", strError.c_str());
        return 1;
    }
    Copy(out / "wallet_sqlite" / "types-512.sqlite", out / "wallet_recover" / "types-512.sqlite");
    WriteLoadSeed(out / "wallet_load" / "types.records", small);
    return 0;
}
