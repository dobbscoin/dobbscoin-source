// Copyright (c) 2026 The Dobbscoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// The SQLite wallet store (db.cpp) and the Berkeley DB reader used to migrate
// old wallets (bdbro.cpp). See issue #44.

#include "bdbro.h"
#include "db.h"
#include "random.h"
#include "util.h"
#include "utilstrencodings.h"

#include "data/bdb_fixture.raw.h"

#include <stdio.h>

#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>

#include <sqlite3.h>

using namespace std;
namespace fs = boost::filesystem;

typedef std::vector<unsigned char> Bytes;

namespace
{
// The fixture (src/test/data/bdb_fixture.raw) is a Berkeley DB 4.8 btree that
// db_load wrote, 512-byte pages, holding these 60 records under subdatabase
// "main". Every seventh value is long enough to live on overflow pages.
Bytes FixtureKey(int i)
{
    std::string s = strprintf("key-%03d", i);
    return Bytes(s.begin(), s.end());
}
Bytes FixtureValue(int i)
{
    int n = (i % 7 == 0) ? 300 + i * 10 : 5 + (i * 13) % 50;
    Bytes v;
    for (int j = 0; j < n; j++)
        v.push_back((unsigned char)((i * 31 + j) & 0xff));
    return v;
}
BerkeleyRO::RecordMap ExpectedFixture()
{
    BerkeleyRO::RecordMap m;
    for (int i = 0; i < 60; i++)
        m[FixtureKey(i)] = FixtureValue(i);
    return m;
}
const size_t FIXTURE_PAGESIZE = 512;

Bytes Fixture()
{
    return Bytes(alert_tests::bdb_fixture, alert_tests::bdb_fixture + sizeof(alert_tests::bdb_fixture));
}

void WriteFile(const fs::path& p, const Bytes& b)
{
    FILE* f = fopen(p.string().c_str(), "wb");
    BOOST_REQUIRE(f);
    BOOST_REQUIRE(fwrite(b.data(), 1, b.size(), f) == b.size());
    fclose(f);
}

Bytes ReadFile(const fs::path& p)
{
    Bytes b;
    FILE* f = fopen(p.string().c_str(), "rb");
    if (!f)
        return b;
    unsigned char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        b.insert(b.end(), buf, buf + n);
    fclose(f);
    return b;
}

void Put32(Bytes& b, size_t off, uint32_t v)
{
    for (int i = 0; i < 4; i++)
        b[off + i] = (v >> (8 * i)) & 0xff;
}

/** Page numbers of the given page type in the fixture. */
std::vector<size_t> PagesOfType(const Bytes& b, unsigned char type)
{
    std::vector<size_t> pages;
    for (size_t p = 0; p < b.size() / FIXTURE_PAGESIZE; p++)
        if (b[p * FIXTURE_PAGESIZE + 25] == type)
            pages.push_back(p);
    return pages;
}

bool ReadAllThrows(const fs::path& p, const std::string& strExpect = "")
{
    BerkeleyRO::RecordMap m;
    try {
        BerkeleyRO::ReadAll(p, m);
    } catch (const std::runtime_error& e) {
        if (!strExpect.empty() && std::string(e.what()).find(strExpect) == std::string::npos) {
            BOOST_ERROR("unexpected error text: " << e.what());
            return false;
        }
        return true;
    }
    return false;
}

struct TempDir {
    fs::path path;
    TempDir()
    {
        path = GetTempPath() / strprintf("test_wallet_sqlite_%lu_%i", (unsigned long)GetTime(), (int)GetRand(1000000));
        fs::create_directories(path);
    }
    ~TempDir() { fs::remove_all(path); }
    std::vector<std::string> List() const
    {
        std::vector<std::string> v;
        for (fs::directory_iterator it(path); it != fs::directory_iterator(); ++it)
            v.push_back(it->path().filename().string());
        std::sort(v.begin(), v.end());
        return v;
    }
};

std::vector<CDBEnv::KeyValPair> AsVector(const BerkeleyRO::RecordMap& m)
{
    return std::vector<CDBEnv::KeyValPair>(m.begin(), m.end());
}

/** Direct access to CDB's protected interface, on the in-memory (mock) environment. */
class TestDB : public CDB
{
public:
    TestDB(const std::string& strFile, const char* pszMode = "r+") : CDB(strFile, pszMode) {}
    bool Put(const std::string& k, const std::string& v, bool fOverwrite = true) { return Write(k, v, fOverwrite); }
    bool Get(const std::string& k, std::string& v) { return Read(k, v); }
    bool Del(const std::string& k) { return Erase(k); }
    bool Has(const std::string& k) { return Exists(k); }
    std::vector<std::string> Keys(const std::string* pstrFrom = NULL)
    {
        CDataStream ssStart(SER_DISK, CLIENT_VERSION);
        if (pstrFrom)
            ssStart << *pstrFrom;
        CDBCursor* pcursor = GetCursor(pstrFrom ? &ssStart : NULL);
        std::vector<std::string> keys;
        while (true) {
            CDataStream ssKey(SER_DISK, CLIENT_VERSION), ssValue(SER_DISK, CLIENT_VERSION);
            int ret = ReadAtCursor(pcursor, ssKey, ssValue);
            if (ret == DB_CURSOR_DONE)
                break;
            BOOST_REQUIRE(ret == DB_CURSOR_OK);
            std::string k;
            ssKey >> k;
            keys.push_back(k);
        }
        CloseCursor(pcursor);
        return keys;
    }
};
} // anonymous namespace

BOOST_AUTO_TEST_SUITE(wallet_sqlite_tests)

BOOST_AUTO_TEST_CASE(bdbro_reads_fixture)
{
    TempDir tmp;
    fs::path p = tmp.path / "fixture.dat";
    WriteFile(p, Fixture());
    BOOST_CHECK(BerkeleyRO::IsBerkeleyBtreeFile(p));

    Bytes b = Fixture();
    BOOST_CHECK(!PagesOfType(b, 3).empty()); // internal page: a two-level tree
    BOOST_CHECK(!PagesOfType(b, 7).empty()); // overflow pages

    BerkeleyRO::RecordMap m;
    BerkeleyRO::ReadAll(p, m);
    BOOST_CHECK_EQUAL(m.size(), 60U);
    BOOST_CHECK(m == ExpectedFixture());
}

BOOST_AUTO_TEST_CASE(bdbro_rejects_damage)
{
    TempDir tmp;
    fs::path p = tmp.path / "damaged.dat";
    const Bytes good = Fixture();

    // Missing last page, and a torn last page.
    WriteFile(p, Bytes(good.begin(), good.end() - FIXTURE_PAGESIZE));
    BOOST_CHECK(ReadAllThrows(p, "truncated"));
    WriteFile(p, Bytes(good.begin(), good.end() - 100));
    BOOST_CHECK(ReadAllThrows(p, "truncated"));

    // A page whose LSN is not reset: data may still be in the BDB log.
    Bytes b = good;
    Put32(b, 3 * FIXTURE_PAGESIZE + 4, 2);
    WriteFile(p, b);
    BOOST_CHECK(ReadAllThrows(p, "not closed cleanly"));

    // A leaf page that claims to be another page.
    std::vector<size_t> leaves = PagesOfType(good, 5);
    BOOST_REQUIRE(leaves.size() >= 2);
    b = good;
    Put32(b, leaves.back() * FIXTURE_PAGESIZE + 8, 9999);
    WriteFile(p, b);
    BOOST_CHECK(ReadAllThrows(p, "claims to be page"));

    // An overflow page turned into something else.
    std::vector<size_t> overflows = PagesOfType(good, 7);
    b = good;
    b[overflows[0] * FIXTURE_PAGESIZE + 25] = 5;
    WriteFile(p, b);
    BOOST_CHECK(ReadAllThrows(p));

    // An item offset pointing outside its page.
    b = good;
    size_t leaf = leaves.back() * FIXTURE_PAGESIZE;
    b[leaf + 26] = 0xff;
    b[leaf + 27] = 0xff;
    WriteFile(p, b);
    BOOST_CHECK(ReadAllThrows(p));

    // Not a Berkeley DB file at all.
    Bytes junk(4096, 0x42);
    WriteFile(p, junk);
    BOOST_CHECK(!BerkeleyRO::IsBerkeleyBtreeFile(p));
    BOOST_CHECK(ReadAllThrows(p, "not a Berkeley DB"));
}

BOOST_AUTO_TEST_CASE(bdbro_salvage_skips_bad_pages)
{
    TempDir tmp;
    fs::path p = tmp.path / "salvage.dat";
    const BerkeleyRO::RecordMap expected = ExpectedFixture();

    // Clean file: salvage is simply a full read.
    WriteFile(p, Fixture());
    BerkeleyRO::RecordList rec;
    std::string strReport;
    BOOST_CHECK(BerkeleyRO::Salvage(p, rec, strReport));
    BOOST_CHECK_EQUAL(rec.size(), 60U);

    // Wreck one leaf page: the strict read fails, the scan keeps the rest.
    Bytes b = Fixture();
    std::vector<size_t> leaves = PagesOfType(b, 5);
    size_t victim = leaves[leaves.size() / 2];
    Put32(b, victim * FIXTURE_PAGESIZE + 8, 12345);
    WriteFile(p, b);
    BOOST_CHECK(ReadAllThrows(p));
    BOOST_CHECK(BerkeleyRO::Salvage(p, rec, strReport));
    BOOST_CHECK(rec.size() > 0 && rec.size() < 60U);
    for (size_t i = 0; i < rec.size(); i++) {
        BerkeleyRO::RecordMap::const_iterator it = expected.find(rec[i].first);
        BOOST_REQUIRE(it != expected.end());
        BOOST_CHECK(it->second == rec[i].second);
    }
    BOOST_CHECK(strReport.find("clean read failed") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(migrate_berkeley_wallet)
{
    TempDir tmp;
    CDBEnv env;
    BOOST_REQUIRE(env.Open(tmp.path));
    fs::path pathWallet = tmp.path / "wallet.dat";
    WriteFile(pathWallet, Fixture());

    std::string strError;
    BOOST_REQUIRE_MESSAGE(env.MigrateFromBerkeley("wallet.dat", strError), strError);

    // The wallet is now SQLite and holds exactly the fixture's records.
    Bytes header = ReadFile(pathWallet);
    BOOST_CHECK(header.size() > 16 && memcmp(header.data(), "SQLite format 3", 16) == 0);
    std::vector<CDBEnv::KeyValPair> vRecords;
    BOOST_REQUIRE(ReadSQLiteWalletFile(pathWallet, vRecords, strError));
    BOOST_CHECK(vRecords == AsVector(ExpectedFixture()));
    BOOST_CHECK(env.Verify("wallet.dat", strError) == CDBEnv::VERIFY_OK);

    // The original survives, byte for byte, and nothing else is left behind.
    std::vector<std::string> files = tmp.List();
    BOOST_REQUIRE_EQUAL(files.size(), 2U);
    BOOST_CHECK(files[0] == "wallet.dat");
    BOOST_CHECK(files[1].find("wallet.dat.bdb-") == 0);
    BOOST_CHECK(ReadFile(tmp.path / files[1]) == Fixture());

    // Running it again is a no-op.
    BOOST_CHECK(env.MigrateFromBerkeley("wallet.dat", strError));
    BOOST_CHECK_EQUAL(tmp.List().size(), 2U);

    // backupwallet's path: a consistent copy through the SQLite backup API.
    BOOST_REQUIRE_MESSAGE(env.Backup("wallet.dat", tmp.path / "backup.dat", strError), strError);
    std::vector<CDBEnv::KeyValPair> vBackup;
    BOOST_REQUIRE(ReadSQLiteWalletFile(tmp.path / "backup.dat", vBackup, strError));
    BOOST_CHECK(vBackup == vRecords);
    BOOST_CHECK(!fs::exists(tmp.path / "backup.dat.tmp"));
    BOOST_CHECK(!env.Backup("wallet.dat", pathWallet, strError)); // not onto itself
    env.Close();
}

BOOST_AUTO_TEST_CASE(migrate_refuses_damaged_wallet)
{
    TempDir tmp;
    CDBEnv env;
    BOOST_REQUIRE(env.Open(tmp.path));
    fs::path pathWallet = tmp.path / "wallet.dat";
    const Bytes good = Fixture();

    Bytes truncated(good.begin(), good.end() - 700);
    WriteFile(pathWallet, truncated);
    std::string strError;
    BOOST_CHECK(!env.MigrateFromBerkeley("wallet.dat", strError));
    BOOST_CHECK(strError.find("truncated") != std::string::npos);
    BOOST_CHECK(ReadFile(pathWallet) == truncated);
    BOOST_CHECK_EQUAL(tmp.List().size(), 1U);

    // Opening it as a wallet is refused too, rather than treated as a new one.
    BOOST_CHECK(env.Verify("wallet.dat", strError) == CDBEnv::RECOVER_FAIL);
    env.Close();
}

BOOST_AUTO_TEST_CASE(verify_catches_mismatch)
{
    TempDir tmp;
    fs::path p = tmp.path / "check.dat";
    std::vector<CDBEnv::KeyValPair> vRecords = AsVector(ExpectedFixture());
    std::string strError;
    BOOST_REQUIRE_MESSAGE(CreateSQLiteWalletFile(p, vRecords, strError), strError);
    BOOST_CHECK(VerifySQLiteWalletFile(p, vRecords, strError));

    // One changed byte in one value.
    sqlite3* db = NULL;
    BOOST_REQUIRE(sqlite3_open(p.string().c_str(), &db) == SQLITE_OK);
    BOOST_REQUIRE(sqlite3_exec(db, "UPDATE main SET value = X'00' || substr(value, 2) WHERE key = CAST('key-005' AS BLOB)", NULL, NULL, NULL) == SQLITE_OK);
    BOOST_CHECK_EQUAL(sqlite3_changes(db), 1);
    sqlite3_close(db);
    BOOST_CHECK(!VerifySQLiteWalletFile(p, vRecords, strError));
    BOOST_CHECK(strError.find("differs") != std::string::npos);

    // One record missing.
    BOOST_REQUIRE(CreateSQLiteWalletFile(p, vRecords, strError));
    BOOST_REQUIRE(sqlite3_open(p.string().c_str(), &db) == SQLITE_OK);
    BOOST_REQUIRE(sqlite3_exec(db, "DELETE FROM main WHERE key = CAST('key-042' AS BLOB)", NULL, NULL, NULL) == SQLITE_OK);
    sqlite3_close(db);
    BOOST_CHECK(!VerifySQLiteWalletFile(p, vRecords, strError));
    BOOST_CHECK(strError.find("record count") != std::string::npos);

    // A file that is not a wallet at all.
    BOOST_REQUIRE(sqlite3_open((tmp.path / "other.db").string().c_str(), &db) == SQLITE_OK);
    BOOST_REQUIRE(sqlite3_exec(db, "CREATE TABLE main(key BLOB PRIMARY KEY, value BLOB)", NULL, NULL, NULL) == SQLITE_OK);
    sqlite3_close(db);
    BOOST_CHECK(!VerifySQLiteWalletFile(tmp.path / "other.db", std::vector<CDBEnv::KeyValPair>(), strError));
    BOOST_CHECK(strError.find("not a Dobbscoin wallet") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(cdb_semantics)
{
    // bitdb is the in-memory environment the test fixture set up.
    const std::string strFile = "cdb_semantics.dat";
    {
        TestDB db(strFile, "cr+");
        std::string v;
        BOOST_CHECK(db.Has("version")); // written by the constructor in "c" mode
        BOOST_CHECK(db.Put("b", "2"));
        BOOST_CHECK(db.Put("a", "1"));
        BOOST_CHECK(db.Put("c", "3"));
        BOOST_CHECK(db.Put("ab", "12"));
        BOOST_CHECK(db.Get("a", v) && v == "1");
        BOOST_CHECK(!db.Put("a", "one", false)); // no overwrite
        BOOST_CHECK(db.Get("a", v) && v == "1");
        BOOST_CHECK(db.Put("a", "one"));
        BOOST_CHECK(db.Get("a", v) && v == "one");
        BOOST_CHECK(db.Del("nonexistent"));
        BOOST_CHECK(!db.Get("nonexistent", v));

        // Key order is the serialized key's byte order, as in Berkeley DB: a
        // std::string serializes as its length, then its bytes.
        std::vector<std::string> keys = db.Keys();
        std::vector<std::string> expect;
        expect.push_back("a");
        expect.push_back("b");
        expect.push_back("c");
        expect.push_back("ab");
        expect.push_back("version");
        BOOST_CHECK(keys == expect);
        std::string strFrom = "b";
        keys = db.Keys(&strFrom);
        expect.erase(expect.begin());
        BOOST_CHECK(keys == expect);

        // Transactions: abort rolls back, commit keeps.
        BOOST_CHECK(db.TxnBegin());
        BOOST_CHECK(!db.TxnBegin());
        {
            TestDB db2(strFile);
            BOOST_CHECK(!db2.TxnBegin()); // one transaction per file at a time
        }
        BOOST_CHECK(db.Put("t", "x"));
        BOOST_CHECK(db.Del("b"));
        BOOST_CHECK(db.TxnAbort());
        BOOST_CHECK(!db.Has("t"));
        BOOST_CHECK(db.Has("b"));
        BOOST_CHECK(db.TxnBegin());
        BOOST_CHECK(db.Put("t", "x"));
        BOOST_CHECK(db.TxnCommit());
        BOOST_CHECK(db.Has("t"));

        // A handle closed mid-transaction aborts it.
        {
            TestDB db3(strFile);
            BOOST_CHECK(db3.TxnBegin());
            BOOST_CHECK(db3.Put("gone", "x"));
        }
        BOOST_CHECK(!db.Has("gone"));

        // Another handle on the same file, same thread, writes inside the open
        // transaction (one connection per file). CWallet's keypool batch relies
        // on this: GenerateNewKey writes the key through its own CWalletDB.
        BOOST_CHECK(db.TxnBegin());
        {
            TestDB db4(strFile);
            BOOST_CHECK(db4.Put("nested", "x"));
        }
        BOOST_CHECK(db.Has("nested"));
        BOOST_CHECK(db.TxnAbort());
        BOOST_CHECK(!db.Has("nested"));
        BOOST_CHECK(db.TxnBegin());
        {
            TestDB db4(strFile);
            BOOST_CHECK(db4.Put("nested", "x"));
        }
        BOOST_CHECK(db.TxnCommit());
        BOOST_CHECK(db.Has("nested"));
        BOOST_CHECK(db.Del("nested"));
    }

    // Rewrite drops records by prefix ("\x01b" = the serialized key "b").
    BOOST_CHECK(CDB::Rewrite(strFile, "\x01" "b"));
    {
        TestDB db(strFile);
        std::string v;
        BOOST_CHECK(!db.Has("b"));
        BOOST_CHECK(db.Has("ab") && db.Has("a") && db.Has("c") && db.Has("t"));
        BOOST_CHECK(db.Get("a", v) && v == "one");
    }
}

BOOST_AUTO_TEST_SUITE_END()
