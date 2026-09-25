// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2026 The Dobbscoin Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "db.h"

#include "bdbro.h"
#include "util.h"
#include "utilstrencodings.h"

#include <stdint.h>

#ifndef WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <boost/filesystem.hpp>
#include <boost/thread.hpp>
#include <boost/thread/recursive_mutex.hpp>

#include <sqlite3.h>

using namespace std;
using namespace boost;

unsigned int nWalletDBUpdated;

namespace
{
/** "BOBW" in the SQLite header's application_id field: this file is a Dobbscoin wallet. */
const int32_t WALLET_APPLICATION_ID = 0x424f4257;
/** Layout version in the header's user_version field: 1 = main(key BLOB PRIMARY KEY, value BLOB). */
const int32_t WALLET_SCHEMA_VERSION = 1;

typedef std::vector<unsigned char> Bytes;

/** Remove a file if it exists; never throws (these are cleanup paths). */
void RemoveQuietly(const boost::filesystem::path& p)
{
    boost::system::error_code ec;
    boost::filesystem::remove(p, ec);
}

std::string SQLiteError(sqlite3* db, int rc)
{
    if (db)
        return strprintf("%s (%d)", sqlite3_errmsg(db), rc);
    return strprintf("%s (%d)", sqlite3_errstr(rc), rc);
}

bool Exec(sqlite3* db, const char* sql, std::string& strError)
{
    char* errmsg = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        strError = strprintf("%s: %s (%d)", sql, errmsg ? errmsg : sqlite3_errstr(rc), rc);
        sqlite3_free(errmsg);
        return false;
    }
    return true;
}

bool PragmaInt(sqlite3* db, const char* sql, int64_t& nOut, std::string& strError)
{
    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        strError = strprintf("%s: %s", sql, SQLiteError(db, rc));
        return false;
    }
    rc = sqlite3_step(stmt);
    bool fOk = (rc == SQLITE_ROW);
    if (fOk)
        nOut = sqlite3_column_int64(stmt, 0);
    else
        strError = strprintf("%s: %s", sql, SQLiteError(db, rc));
    sqlite3_finalize(stmt);
    return fOk;
}

/**
 * Connection settings. A wallet has one writer, so no WAL: a rollback journal
 * that is deleted on commit keeps wallet.dat a single self-contained file that
 * can be copied while the node is stopped. synchronous=FULL makes a commit
 * durable before it returns. secure_delete overwrites freed content, so an
 * erased plaintext key does not linger in a free page after encryptwallet.
 */
bool ConfigureConnection(sqlite3* db, std::string& strError)
{
    sqlite3_extended_result_codes(db, 1);
    sqlite3_busy_timeout(db, 5000);
    // Normal (not exclusive) locking: the journal is removed after every
    // commit instead of lingering beside wallet.dat, and the datadir lock
    // already keeps a second node away.
    // EXTRA, not FULL: in DELETE mode the commit point is unlinking the journal,
    // and only EXTRA syncs the directory after that. At FULL a power cut right
    // after a commit can bring the journal back and roll the commit back,
    // e.g. the key a payment was just sent to (verified with strace).
    return Exec(db, "PRAGMA journal_mode = DELETE", strError) &&
           Exec(db, "PRAGMA synchronous = EXTRA", strError) &&
           Exec(db, "PRAGMA secure_delete = ON", strError) &&
           Exec(db, "PRAGMA fullfsync = ON", strError);
}

/** Create the schema in an empty database, or check an existing one is ours. */
bool SetupSchema(sqlite3* db, bool fCreate, std::string& strError)
{
    int64_t nTables = 0;
    if (!PragmaInt(db, "SELECT count(*) FROM sqlite_master WHERE type = 'table'", nTables, strError))
        return false;
    if (nTables == 0) {
        if (!fCreate) {
            strError = "database is empty";
            return false;
        }
        std::string sql = strprintf("BEGIN; CREATE TABLE main(key BLOB PRIMARY KEY NOT NULL, value BLOB NOT NULL); "
                                    "PRAGMA application_id = %d; PRAGMA user_version = %d; COMMIT;",
                                    WALLET_APPLICATION_ID, WALLET_SCHEMA_VERSION);
        if (!Exec(db, sql.c_str(), strError)) {
            std::string strIgnored;
            Exec(db, "ROLLBACK", strIgnored);
            return false;
        }
        return true;
    }
    int64_t nAppId = 0, nUserVersion = 0;
    if (!PragmaInt(db, "PRAGMA application_id", nAppId, strError) ||
        !PragmaInt(db, "PRAGMA user_version", nUserVersion, strError))
        return false;
    if (nAppId != WALLET_APPLICATION_ID) {
        strError = strprintf("not a Dobbscoin wallet (SQLite application_id is 0x%08x)", (uint32_t)nAppId);
        return false;
    }
    if (nUserVersion != WALLET_SCHEMA_VERSION) {
        strError = strprintf("unsupported wallet schema version %d", (int)nUserVersion);
        return false;
    }
    return true;
}

void BindBlob(sqlite3_stmt* stmt, int col, const unsigned char* p, size_t n)
{
    static const unsigned char empty = 0; // a NULL pointer would bind SQL NULL, not a zero-length blob
    sqlite3_bind_blob(stmt, col, n ? p : &empty, (int)n, SQLITE_TRANSIENT);
}

const unsigned char* StreamData(const CDataStream& ss)
{
    return ss.empty() ? NULL : (const unsigned char*)&ss.begin()[0];
}

Bytes ColumnBytes(sqlite3_stmt* stmt, int col)
{
    const unsigned char* p = (const unsigned char*)sqlite3_column_blob(stmt, col);
    int n = sqlite3_column_bytes(stmt, col);
    return (p && n > 0) ? Bytes(p, p + n) : Bytes();
}

/** Open a wallet file outside the CDBEnv registry (migration, rewrite, verify). */
sqlite3* OpenStandalone(const boost::filesystem::path& path, bool fCreate, bool fReadOnly, std::string& strError)
{
    sqlite3* db = NULL;
    int flags = (fReadOnly ? SQLITE_OPEN_READONLY : SQLITE_OPEN_READWRITE) | SQLITE_OPEN_FULLMUTEX;
    if (fCreate)
        flags |= SQLITE_OPEN_CREATE;
    int rc = sqlite3_open_v2(path.string().c_str(), &db, flags, NULL);
    if (rc != SQLITE_OK) {
        strError = strprintf("cannot open %s: %s", path.string(), SQLiteError(db, rc));
        sqlite3_close(db);
        return NULL;
    }
    // SQLITE_OPEN_READWRITE quietly falls back to read-only when the file (or its
    // directory) is write-protected. The pragmas and BEGIN IMMEDIATE still succeed;
    // only the first INSERT fails, long after startup. Berkeley DB refused such a
    // file at load, so refuse it here too, with a message that says why.
    if (!fReadOnly && sqlite3_db_readonly(db, "main") == 1) {
        strError = strprintf("cannot write to %s, it is read-only. Check that the user running dobbscoin can write to that file and to its folder.", path.string());
        sqlite3_close(db);
        return NULL;
    }
    if ((!fReadOnly && !ConfigureConnection(db, strError)) || !SetupSchema(db, fCreate, strError)) {
        strError = strprintf("%s: %s", path.string(), strError);
        sqlite3_close(db);
        return NULL;
    }
    return db;
}

bool ReadAllRecords(sqlite3* db, std::vector<CDBEnv::KeyValPair>& vRecords, std::string& strError)
{
    vRecords.clear();
    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(db, "SELECT key, value FROM main ORDER BY key", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        strError = SQLiteError(db, rc);
        return false;
    }
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
        vRecords.push_back(std::make_pair(ColumnBytes(stmt, 0), ColumnBytes(stmt, 1)));
    if (rc != SQLITE_DONE)
        strError = SQLiteError(db, rc);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

} // anonymous namespace

/** Write vRecords into a brand-new file at path, in one transaction. */
bool CreateSQLiteWalletFile(const boost::filesystem::path& path, const std::vector<CDBEnv::KeyValPair>& vRecords, std::string& strError)
{
    RemoveQuietly(path);
    RemoveQuietly(path.string() + "-journal");
    boost::system::error_code ecExists; // the error_code form never throws; an unreadable
                                        // path fails at the SQLite open below instead
    if (boost::filesystem::exists(path, ecExists)) {
        strError = strprintf("cannot remove the stale file %s", path.string());
        return false;
    }
    sqlite3* db = OpenStandalone(path, true, false, strError);
    if (!db)
        return false;
    bool fOk = Exec(db, "BEGIN", strError);
    sqlite3_stmt* stmt = NULL;
    if (fOk) {
        int rc = sqlite3_prepare_v2(db, "INSERT INTO main (key, value) VALUES (?, ?)", -1, &stmt, NULL);
        if (rc != SQLITE_OK) {
            strError = SQLiteError(db, rc);
            fOk = false;
        }
    }
    for (size_t i = 0; fOk && i < vRecords.size(); i++) {
        BindBlob(stmt, 1, vRecords[i].first.data(), vRecords[i].first.size());
        BindBlob(stmt, 2, vRecords[i].second.data(), vRecords[i].second.size());
        int rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            strError = strprintf("inserting record %u: %s", (unsigned int)i, SQLiteError(db, rc));
            fOk = false;
        }
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
    }
    sqlite3_finalize(stmt);
    std::string strIgnored;
    if (fOk)
        fOk = Exec(db, "COMMIT", strError);
    else
        Exec(db, "ROLLBACK", strIgnored);
    int rc = sqlite3_close(db);
    if (fOk && rc != SQLITE_OK) {
        strError = strprintf("closing %s: %s", path.string(), sqlite3_errstr(rc));
        fOk = false;
    }
    return fOk;
}

bool ReadSQLiteWalletFile(const boost::filesystem::path& path, std::vector<CDBEnv::KeyValPair>& vRecords, std::string& strError)
{
    sqlite3* db = OpenStandalone(path, false, true, strError);
    if (!db)
        return false;
    bool fOk = ReadAllRecords(db, vRecords, strError);
    sqlite3_close(db);
    return fOk;
}

/** Read the file back and require exactly vExpected (any order), byte for byte. */
bool VerifySQLiteWalletFile(const boost::filesystem::path& path, std::vector<CDBEnv::KeyValPair> vExpected, std::string& strError)
{
    sqlite3* db = OpenStandalone(path, false, true, strError);
    if (!db)
        return false;
    std::vector<CDBEnv::KeyValPair> vActual;
    bool fOk = ReadAllRecords(db, vActual, strError);
    std::string strCheck;
    int64_t nIgnored;
    // integrity_check, not quick_check: only the full check compares each index
    // with its table, and a wallet whose key index disagrees with its rows loads
    // with records silently missing (found by the wallet_sqlite fuzzer).
    if (fOk && !PragmaInt(db, "PRAGMA integrity_check", nIgnored, strCheck)) {
        strError = "integrity_check failed: " + strCheck;
        fOk = false;
    }
    sqlite3_close(db);
    if (!fOk)
        return false;
    std::sort(vExpected.begin(), vExpected.end());
    if (vActual.size() != vExpected.size()) {
        strError = strprintf("record count differs after writing: expected %u, read back %u",
                             (unsigned int)vExpected.size(), (unsigned int)vActual.size());
        return false;
    }
    for (size_t i = 0; i < vActual.size(); i++) {
        if (vActual[i] != vExpected[i]) {
            strError = strprintf("record %u differs after writing (key %s)", (unsigned int)i,
                                 HexStr(vExpected[i].first.begin(), vExpected[i].first.end()));
            return false;
        }
    }
    return true;
}

namespace
{
bool FilesIdentical(const boost::filesystem::path& a, const boost::filesystem::path& b)
{
    FILE* fa = fopen(a.string().c_str(), "rb");
    FILE* fb = fopen(b.string().c_str(), "rb");
    bool fSame = fa && fb;
    char ba[65536], bb[65536];
    while (fSame) {
        size_t na = fread(ba, 1, sizeof(ba), fa);
        size_t nb = fread(bb, 1, sizeof(bb), fb);
        if (na != nb || memcmp(ba, bb, na) != 0)
            fSame = false;
        if (na == 0)
            break;
    }
    if (fa)
        fclose(fa);
    if (fb)
        fclose(fb);
    return fSame;
}

void SyncDirectory(const boost::filesystem::path& dir)
{
#ifndef WIN32
    int fd = open(dir.string().c_str(), O_RDONLY);
    if (fd >= 0) {
        fsync(fd);
        close(fd);
    }
#endif
}

/** The file a wallet path really is. wallet.dat may be a symlink to another
 *  volume (an encrypted disk, say). Replacing it must happen beside the target:
 *  renaming over the link itself swaps it for a plain file in the datadir, so
 *  the wallet silently moves off the volume the user put it on. */
boost::filesystem::path RealWalletPath(const boost::filesystem::path& p)
{
    boost::system::error_code ec;
    if (boost::filesystem::is_symlink(p, ec)) {
        boost::filesystem::path r = boost::filesystem::canonical(p, ec);
        if (!ec)
            return r;
    }
    return p;
}
} // anonymous namespace

std::string WalletDBVersion()
{
    return sqlite3_libversion();
}

/** One open wallet file. The mutex serialises statements and is held for the
 *  whole of a transaction, so a write from another thread waits for the
 *  transaction to finish -- as it did under Berkeley DB's locks -- instead of
 *  landing inside it. */
class CSQLiteFile
{
public:
    sqlite3* db;
    boost::recursive_mutex mutex;

    CSQLiteFile() : db(NULL) {}
    ~CSQLiteFile()
    {
        if (db)
            sqlite3_close(db);
    }
};

class CDBCursor
{
public:
    CSQLiteFile* pfile;
    bool fStarted;
    bool fDone;
    Bytes vchLastKey;
    Bytes vchStartKey;
    bool fHaveStart;

    CDBCursor() : pfile(NULL), fStarted(false), fDone(false), fHaveStart(false) {}
};


//
// CDBEnv
//

CDBEnv bitdb;

CDBEnv::CDBEnv()
{
    fDbEnvInit = false;
    fMockDb = false;
}

CDBEnv::~CDBEnv()
{
    // Close() without its LOCK: this runs during static destruction, when no
    // other thread can use bitdb any more, and with -DDEBUG_LOCKORDER the lock
    // checker's own statics may already be destroyed.
    for (map<string, CSQLiteFile*>::iterator it = mapDb.begin(); it != mapDb.end(); ++it)
        delete it->second;
    mapDb.clear();
}

void CDBEnv::Close()
{
    LOCK(cs_db);
    for (map<string, CSQLiteFile*>::iterator it = mapDb.begin(); it != mapDb.end(); ++it)
        delete it->second;
    mapDb.clear();
    fDbEnvInit = false;
}

bool CDBEnv::Open(const boost::filesystem::path& pathIn)
{
    if (fDbEnvInit)
        return true;

    boost::this_thread::interruption_point();

    path = pathIn;
    int rc = sqlite3_initialize(); // required: depends builds SQLite with SQLITE_OMIT_AUTOINIT
    if (rc != SQLITE_OK)
        return error("CDBEnv::Open : sqlite3_initialize failed: %s", sqlite3_errstr(rc));
    if (sqlite3_threadsafe() == 0)
        return error("CDBEnv::Open : SQLite was built without thread safety");
    LogPrintf("CDBEnv::Open : wallet storage is SQLite %s in %s\n", sqlite3_libversion(), path.string());
    fDbEnvInit = true;
    fMockDb = false;
    return true;
}

void CDBEnv::MakeMock()
{
    if (fDbEnvInit)
        throw runtime_error("CDBEnv::MakeMock : Already initialized");

    boost::this_thread::interruption_point();

    LogPrint("db", "CDBEnv::MakeMock\n");
    if (sqlite3_initialize() != SQLITE_OK)
        throw runtime_error("CDBEnv::MakeMock : sqlite3_initialize failed");
    fDbEnvInit = true;
    fMockDb = true;
}

CSQLiteFile* CDBEnv::OpenFile(const std::string& strFile, bool fCreate)
{
    AssertLockHeld(cs_db);
    CSQLiteFile*& pfile = mapDb[strFile];
    if (pfile)
        return pfile;

    std::string strError;
    sqlite3* db = NULL;
    if (fMockDb) {
        int rc = sqlite3_open_v2(":memory:", &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, NULL);
        if (rc != SQLITE_OK || !SetupSchema(db, true, strError)) {
            sqlite3_close(db);
            mapDb.erase(strFile);
            throw runtime_error(strprintf("CDB : cannot create in-memory database %s: %s", strFile, strError));
        }
    } else {
        boost::filesystem::path pathFile = path / strFile;
        if (boost::filesystem::exists(pathFile) && BerkeleyRO::IsBerkeleyBtreeFile(pathFile)) {
            mapDb.erase(strFile);
            throw runtime_error(strprintf("CDB : %s is still a Berkeley DB file; it is migrated at startup", strFile));
        }
        if (!fCreate && !boost::filesystem::exists(pathFile)) {
            mapDb.erase(strFile);
            throw runtime_error(strprintf("CDB : database %s does not exist", strFile));
        }
        db = OpenStandalone(pathFile, fCreate, false, strError);
        if (!db) {
            mapDb.erase(strFile);
            throw runtime_error(strprintf("CDB : can't open database %s: %s", strFile, strError));
        }
    }
    pfile = new CSQLiteFile();
    pfile->db = db;
    return pfile;
}

CDBEnv::VerifyResult CDBEnv::Verify(const std::string& strFile, std::string& strError)
{
    LOCK(cs_db);
    assert(mapFileUseCount.count(strFile) == 0);
    if (fMockDb)
        return VERIFY_OK;

    boost::filesystem::path pathFile = path / strFile;
    if (BerkeleyRO::IsBerkeleyBtreeFile(pathFile)) {
        strError = "still a Berkeley DB file";
        return RECOVER_FAIL;
    }
    sqlite3* db = OpenStandalone(pathFile, false, false, strError);
    if (!db)
        return RECOVER_FAIL;
    sqlite3_stmt* stmt = NULL;
    VerifyResult result = RECOVER_FAIL;
    // Not quick_check: it does not compare the key index with the table, and a
    // wallet where they disagree passes it yet loads with a key silently missing.
    if (sqlite3_prepare_v2(db, "PRAGMA integrity_check", -1, &stmt, NULL) == SQLITE_OK) {
        int rc = sqlite3_step(stmt);
        std::string strResult;
        if (rc == SQLITE_ROW && sqlite3_column_text(stmt, 0))
            strResult = (const char*)sqlite3_column_text(stmt, 0);
        if (strResult == "ok")
            result = VERIFY_OK;
        else
            strError = strResult.empty() ? SQLiteError(db, rc) : strResult;
    } else {
        strError = sqlite3_errmsg(db);
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return result;
}

bool CDBEnv::ReplaceWithSQLite(const std::string& strFile, const std::vector<KeyValPair>& vRecords,
                               const std::string& strBackupTag, std::string& strBackupName, std::string& strError)
{
    LOCK(cs_db);
    assert(!fMockDb);
    boost::filesystem::path pathFile = RealWalletPath(path / strFile);
    boost::filesystem::path pathTmp = pathFile.string() + ".migrating";

    // 1. Write the records to a new file beside the wallet. A leftover from an
    //    interrupted attempt is simply replaced: the original was never touched.
    if (!CreateSQLiteWalletFile(pathTmp, vRecords, strError)) {
        RemoveQuietly(pathTmp);
        RemoveQuietly(pathTmp.string() + "-journal");
        return false;
    }

    // 2. Read it back and compare every record byte for byte.
    if (!VerifySQLiteWalletFile(pathTmp, vRecords, strError)) {
        strError = "verification failed: " + strError;
        RemoveQuietly(pathTmp);
        return false;
    }

    // 3. Keep the original under a new name. A hard link is the original file
    //    itself (same inode, no copy to get wrong); where links are not
    //    supported, a copy that is compared with the original.
    int64_t nNow = GetTime();
    boost::filesystem::path pathBackup = pathFile.string() + strprintf(".%s-%d", strBackupTag, nNow);
    for (int i = 1; boost::filesystem::exists(pathBackup); i++)
        pathBackup = pathFile.string() + strprintf(".%s-%d-%d", strBackupTag, nNow, i);
    try {
        boost::system::error_code ec;
        boost::filesystem::create_hard_link(pathFile, pathBackup, ec);
        if (ec) {
            LogPrintf("CDBEnv::ReplaceWithSQLite : hard link failed (%s), copying instead\n", ec.message());
            boost::filesystem::copy_file(pathFile, pathBackup);
            FILE* f = fopen(pathBackup.string().c_str(), "rb+");
            if (f) {
                FileCommit(f);
                fclose(f);
            }
            if (!FilesIdentical(pathFile, pathBackup)) {
                RemoveQuietly(pathBackup);
                throw runtime_error("the copy of the original does not match it");
            }
        }
        SyncDirectory(pathFile.parent_path());
    } catch (const std::exception& e) {
        strError = strprintf("could not preserve the original as %s: %s", pathBackup.string(), e.what());
        RemoveQuietly(pathTmp);
        return false;
    }

    // 4. Atomically put the new file in place. Until this rename the wallet
    //    path still holds the untouched original.
    try {
        boost::filesystem::rename(pathTmp, pathFile);
        SyncDirectory(pathFile.parent_path());
    } catch (const std::exception& e) {
        strError = strprintf("could not move %s into place: %s", pathTmp.string(), e.what());
        return false;
    }
    strBackupName = pathBackup.filename().string();
    return true;
}

bool CDBEnv::MigrateFromBerkeley(const std::string& strFile, std::string& strError)
{
    boost::filesystem::path pathFile = path / strFile;
    if (fMockDb || !boost::filesystem::exists(pathFile) || !BerkeleyRO::IsBerkeleyBtreeFile(pathFile))
        return true;

    LogPrintf("Wallet %s is a Berkeley DB file; migrating it to SQLite %s\n", strFile, sqlite3_libversion());
    int64_t nStart = GetTimeMillis();
    BerkeleyRO::RecordMap mapRecords;
    try {
        BerkeleyRO::ReadAll(pathFile, mapRecords);
    } catch (const std::exception& e) {
        strError = strprintf("could not read the Berkeley DB wallet: %s", e.what());
        return false;
    }
    std::vector<KeyValPair> vRecords(mapRecords.begin(), mapRecords.end());
    LogPrintf("Read %u records from %s\n", (unsigned int)vRecords.size(), strFile);

    std::string strBackup;
    if (!ReplaceWithSQLite(strFile, vRecords, "bdb", strBackup, strError))
        return false;
    LogPrintf("Migrated %s to SQLite: %u records written and verified byte for byte in %dms; "
              "the Berkeley DB original is kept as %s\n",
              strFile, (unsigned int)vRecords.size(), GetTimeMillis() - nStart, strBackup);
    return true;
}

bool CDBEnv::Backup(const std::string& strFile, const boost::filesystem::path& pathDest, std::string& strError)
{
    boost::filesystem::path pathTmp = pathDest.string() + ".tmp";
    CSQLiteFile* pfile;
    {
        LOCK(cs_db);
        if (!fMockDb) {
            boost::system::error_code ec;
            if (boost::filesystem::exists(pathDest, ec) && boost::filesystem::equivalent(path / strFile, pathDest, ec)) {
                strError = "cannot back up a wallet onto itself";
                return false;
            }
        }
        try {
            pfile = OpenFile(strFile, false);
        } catch (const std::exception& e) {
            strError = e.what();
            return false;
        }
        ++mapFileUseCount[strFile]; // keeps the connection open while we copy
    }
    // Give the use count back however this function ends. A count left at 1
    // (an exception between here and the end) makes CDB::Rewrite, which waits
    // for 0, spin forever: the next encryptwallet would hang the node.
    struct UseCountGuard {
        CDBEnv& env;
        const std::string& strFile;
        ~UseCountGuard()
        {
            LOCK(env.cs_db);
            --env.mapFileUseCount[strFile];
        }
    } useCountGuard = {*this, strFile};

    bool fOk;
    {
        // The file mutex is held for the whole of any transaction, so taking
        // it means no half-finished transaction is copied.
        boost::lock_guard<boost::recursive_mutex> lock(pfile->mutex);
        std::vector<KeyValPair> vRecords;
        fOk = ReadAllRecords(pfile->db, vRecords, strError);
        sqlite3* dst = NULL;
        if (fOk) {
            RemoveQuietly(pathTmp);
            RemoveQuietly(pathTmp.string() + "-journal");
            int rc = sqlite3_open_v2(pathTmp.string().c_str(), &dst, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
            if (rc != SQLITE_OK) {
                strError = strprintf("cannot create %s: %s", pathTmp.string(), SQLiteError(dst, rc));
                fOk = false;
            }
        }
        if (fOk) {
            sqlite3_backup* pbackup = sqlite3_backup_init(dst, "main", pfile->db, "main");
            if (!pbackup) {
                strError = SQLiteError(dst, sqlite3_errcode(dst));
                fOk = false;
            } else {
                int rc = sqlite3_backup_step(pbackup, -1);
                sqlite3_backup_finish(pbackup);
                if (rc != SQLITE_DONE) {
                    strError = strprintf("backup failed: %s", sqlite3_errstr(rc));
                    fOk = false;
                }
            }
        }
        if (dst && sqlite3_close(dst) != SQLITE_OK && fOk) {
            strError = "closing the backup failed";
            fOk = false;
        }
        // The copy must hold exactly what the wallet holds.
        if (fOk)
            fOk = VerifySQLiteWalletFile(pathTmp, vRecords, strError);
    }
    if (fOk) {
        try {
            boost::filesystem::rename(pathTmp, pathDest);
            SyncDirectory(pathDest.parent_path().empty() ? boost::filesystem::path(".") : pathDest.parent_path());
        } catch (const std::exception& e) {
            strError = e.what();
            fOk = false;
        }
    }
    if (!fOk)
        RemoveQuietly(pathTmp);
    return fOk;
}

void CDBEnv::CloseDb(const string& strFile)
{
    LOCK(cs_db);
    if (fMockDb)
        return; // an in-memory database lives only as long as its connection
    map<string, CSQLiteFile*>::iterator it = mapDb.find(strFile);
    if (it != mapDb.end()) {
        delete it->second;
        mapDb.erase(it);
    }
}

bool CDBEnv::RemoveDb(const string& strFile)
{
    LOCK(cs_db);
    map<string, CSQLiteFile*>::iterator it = mapDb.find(strFile);
    if (it != mapDb.end()) {
        delete it->second;
        mapDb.erase(it);
    }
    if (fMockDb)
        return true;
    boost::system::error_code ec;
    boost::filesystem::remove(path / strFile, ec);
    return !ec;
}

void CDBEnv::Flush(bool fShutdown)
{
    int64_t nStart = GetTimeMillis();
    // Every commit is already on disk; flushing only closes files nobody is using.
    LogPrint("db", "CDBEnv::Flush : Flush(%s)%s\n", fShutdown ? "true" : "false", fDbEnvInit ? "" : " database not started");
    if (!fDbEnvInit)
        return;
    {
        LOCK(cs_db);
        map<string, int>::iterator mi = mapFileUseCount.begin();
        while (mi != mapFileUseCount.end()) {
            string strFile = (*mi).first;
            int nRefCount = (*mi).second;
            LogPrint("db", "CDBEnv::Flush : Flushing %s (refcount = %d)...\n", strFile, nRefCount);
            if (nRefCount == 0) {
                CloseDb(strFile);
                LogPrint("db", "CDBEnv::Flush : %s closed\n", strFile);
                mapFileUseCount.erase(mi++);
            } else
                mi++;
        }
        LogPrint("db", "CDBEnv::Flush : Flush(%s)%s took %15dms\n", fShutdown ? "true" : "false", fDbEnvInit ? "" : " database not started", GetTimeMillis() - nStart);
        if (fShutdown && mapFileUseCount.empty())
            Close();
    }
}


//
// CDB
//

CDB::CDB(const std::string& strFilename, const char* pszMode) : pdb(NULL), activeTxn(false)
{
    fReadOnly = (!strchr(pszMode, '+') && !strchr(pszMode, 'w'));
    if (strFilename.empty())
        return;

    bool fCreate = strchr(pszMode, 'c') != NULL;

    {
        LOCK(bitdb.cs_db);
        if (!bitdb.Open(GetDataDir()))
            throw runtime_error("CDB : Failed to open database environment.");

        pdb = bitdb.OpenFile(strFilename, fCreate); // throws on failure
        strFile = strFilename;
        ++bitdb.mapFileUseCount[strFile];

        if (fCreate && !Exists(string("version"))) {
            bool fTmp = fReadOnly;
            fReadOnly = false;
            WriteVersion(CLIENT_VERSION);
            fReadOnly = fTmp;
        }
    }
}

void CDB::Flush()
{
    // Nothing to do: a committed write is on disk.
}

void CDB::Close()
{
    if (!pdb)
        return;
    if (activeTxn)
        TxnAbort();
    pdb = NULL;

    {
        LOCK(bitdb.cs_db);
        --bitdb.mapFileUseCount[strFile];
    }
}

bool CDB::ReadRaw(const CDataStream& ssKey, CDataStream& ssValue)
{
    boost::lock_guard<boost::recursive_mutex> lock(pdb->mutex);
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(pdb->db, "SELECT value FROM main WHERE key = ?", -1, &stmt, NULL) != SQLITE_OK)
        return false;
    BindBlob(stmt, 1, StreamData(ssKey), ssKey.size());
    bool fFound = false;
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        const char* p = (const char*)sqlite3_column_blob(stmt, 0);
        int n = sqlite3_column_bytes(stmt, 0);
        ssValue.SetType(SER_DISK);
        ssValue.clear();
        if (p && n > 0)
            ssValue.write(p, n);
        fFound = true;
    } else if (rc != SQLITE_DONE) {
        LogPrintf("CDB::Read : %s\n", SQLiteError(pdb->db, rc));
    }
    sqlite3_finalize(stmt); // secure_delete does not cover the page cache; finalize frees our copy
    return fFound;
}

bool CDB::WriteRaw(const CDataStream& ssKey, const CDataStream& ssValue, bool fOverwrite)
{
    boost::lock_guard<boost::recursive_mutex> lock(pdb->mutex);
    sqlite3_stmt* stmt = NULL;
    const char* sql = fOverwrite ? "INSERT OR REPLACE INTO main (key, value) VALUES (?, ?)"
                                 : "INSERT INTO main (key, value) VALUES (?, ?)";
    int rc = sqlite3_prepare_v2(pdb->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LogPrintf("CDB::Write : %s\n", SQLiteError(pdb->db, rc));
        return false;
    }
    BindBlob(stmt, 1, StreamData(ssKey), ssKey.size());
    BindBlob(stmt, 2, StreamData(ssValue), ssValue.size());
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        // A duplicate key with fOverwrite=false is an expected refusal, not an error.
        if (fOverwrite || (rc & 0xff) != SQLITE_CONSTRAINT)
            LogPrintf("CDB::Write : %s\n", SQLiteError(pdb->db, rc));
        return false;
    }
    return true;
}

bool CDB::EraseRaw(const CDataStream& ssKey)
{
    boost::lock_guard<boost::recursive_mutex> lock(pdb->mutex);
    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(pdb->db, "DELETE FROM main WHERE key = ?", -1, &stmt, NULL);
    if (rc != SQLITE_OK)
        return false;
    BindBlob(stmt, 1, StreamData(ssKey), ssKey.size());
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        LogPrintf("CDB::Erase : %s\n", SQLiteError(pdb->db, rc));
        return false;
    }
    return true; // erasing a missing key is not an error (DB_NOTFOUND was accepted too)
}

bool CDB::ExistsRaw(const CDataStream& ssKey)
{
    boost::lock_guard<boost::recursive_mutex> lock(pdb->mutex);
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(pdb->db, "SELECT 1 FROM main WHERE key = ?", -1, &stmt, NULL) != SQLITE_OK)
        return false;
    BindBlob(stmt, 1, StreamData(ssKey), ssKey.size());
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_ROW;
}

CDBCursor* CDB::GetCursor(const CDataStream* pssStart)
{
    if (!pdb)
        return NULL;
    CDBCursor* pcursor = new CDBCursor();
    pcursor->pfile = pdb;
    if (pssStart) {
        pcursor->fHaveStart = true;
        pcursor->vchStartKey.assign(pssStart->begin(), pssStart->end());
    }
    return pcursor;
}

/**
 * Each step is its own "next key after the last one" query. No statement stays
 * open between steps, so a cursor never holds a read transaction open and
 * never delays another writer's commit.
 */
int CDB::ReadAtCursor(CDBCursor* pcursor, CDataStream& ssKey, CDataStream& ssValue)
{
    if (!pcursor || pcursor->fDone)
        return DB_CURSOR_DONE;
    boost::lock_guard<boost::recursive_mutex> lock(pcursor->pfile->mutex);
    sqlite3* db = pcursor->pfile->db;
    sqlite3_stmt* stmt = NULL;
    const char* sql;
    const Bytes* pBound = NULL;
    if (pcursor->fStarted) {
        sql = "SELECT key, value FROM main WHERE key > ? ORDER BY key LIMIT 1";
        pBound = &pcursor->vchLastKey;
    } else if (pcursor->fHaveStart) {
        sql = "SELECT key, value FROM main WHERE key >= ? ORDER BY key LIMIT 1";
        pBound = &pcursor->vchStartKey;
    } else {
        sql = "SELECT key, value FROM main ORDER BY key LIMIT 1";
    }
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LogPrintf("CDB::ReadAtCursor : %s\n", SQLiteError(db, rc));
        return DB_CURSOR_ERROR;
    }
    if (pBound)
        BindBlob(stmt, 1, pBound->data(), pBound->size());
    rc = sqlite3_step(stmt);
    int ret;
    if (rc == SQLITE_ROW) {
        pcursor->vchLastKey = ColumnBytes(stmt, 0);
        pcursor->fStarted = true;
        ssKey.SetType(SER_DISK);
        ssKey.clear();
        ssKey.write((const char*)pcursor->vchLastKey.data(), pcursor->vchLastKey.size());
        const char* p = (const char*)sqlite3_column_blob(stmt, 1);
        int n = sqlite3_column_bytes(stmt, 1);
        ssValue.SetType(SER_DISK);
        ssValue.clear();
        if (p && n > 0)
            ssValue.write(p, n);
        ret = DB_CURSOR_OK;
    } else if (rc == SQLITE_DONE) {
        pcursor->fDone = true;
        ret = DB_CURSOR_DONE;
    } else {
        LogPrintf("CDB::ReadAtCursor : %s\n", SQLiteError(db, rc));
        ret = DB_CURSOR_ERROR;
    }
    sqlite3_finalize(stmt);
    return ret;
}

void CDB::CloseCursor(CDBCursor* pcursor)
{
    delete pcursor;
}

bool CDB::TxnBegin()
{
    if (!pdb || activeTxn)
        return false;
    pdb->mutex.lock(); // held until commit or abort
    std::string strError;
    // A second transaction on the same file from the same thread is refused,
    // as Berkeley DB would have deadlocked on it.
    if (sqlite3_get_autocommit(pdb->db) == 0 || !Exec(pdb->db, "BEGIN IMMEDIATE", strError)) {
        if (!strError.empty())
            LogPrintf("CDB::TxnBegin : %s\n", strError);
        pdb->mutex.unlock();
        return false;
    }
    activeTxn = true;
    return true;
}

bool CDB::TxnCommit()
{
    if (!pdb || !activeTxn)
        return false;
    std::string strError;
    bool fOk = Exec(pdb->db, "COMMIT", strError);
    if (!fOk) {
        LogPrintf("CDB::TxnCommit : %s\n", strError);
        std::string strIgnored;
        Exec(pdb->db, "ROLLBACK", strIgnored);
    }
    activeTxn = false;
    pdb->mutex.unlock();
    return fOk;
}

bool CDB::TxnAbort()
{
    if (!pdb || !activeTxn)
        return false;
    std::string strError;
    bool fOk = Exec(pdb->db, "ROLLBACK", strError);
    if (!fOk)
        LogPrintf("CDB::TxnAbort : %s\n", strError);
    activeTxn = false;
    pdb->mutex.unlock();
    return fOk;
}

bool CDB::Rewrite(const string& strFile, const char* pszSkip)
{
    while (true) {
        {
            LOCK(bitdb.cs_db);
            if (!bitdb.mapFileUseCount.count(strFile) || bitdb.mapFileUseCount[strFile] == 0) {
                LogPrintf("CDB::Rewrite : Rewriting %s...\n", strFile);
                std::string strError;
                std::vector<CDBEnv::KeyValPair> vRecords;
                bool fSuccess;
                {
                    // Read every record through a normal handle.
                    CSQLiteFile* pfile = bitdb.OpenFile(strFile, false);
                    boost::lock_guard<boost::recursive_mutex> lock(pfile->mutex);
                    fSuccess = ReadAllRecords(pfile->db, vRecords, strError);
                }
                std::vector<CDBEnv::KeyValPair> vKeep;
                for (size_t i = 0; fSuccess && i < vRecords.size(); i++) {
                    const Bytes& key = vRecords[i].first;
                    if (pszSkip && strncmp((const char*)key.data(), pszSkip, std::min(key.size(), strlen(pszSkip))) == 0)
                        continue;
                    if (key.size() >= 8 && memcmp(key.data(), "\x07version", 8) == 0) {
                        CDataStream ssValue(SER_DISK, CLIENT_VERSION);
                        ssValue << CLIENT_VERSION;
                        vRecords[i].second.assign(ssValue.begin(), ssValue.end());
                    }
                    vKeep.push_back(vRecords[i]);
                }

                if (fSuccess && bitdb.IsMock()) {
                    // No file to swap: replace the contents in place.
                    CSQLiteFile* pfile = bitdb.OpenFile(strFile, false);
                    boost::lock_guard<boost::recursive_mutex> lock(pfile->mutex);
                    sqlite3_stmt* stmt = NULL;
                    fSuccess = Exec(pfile->db, "BEGIN; DELETE FROM main;", strError) &&
                               sqlite3_prepare_v2(pfile->db, "INSERT INTO main (key, value) VALUES (?, ?)", -1, &stmt, NULL) == SQLITE_OK;
                    for (size_t i = 0; fSuccess && i < vKeep.size(); i++) {
                        BindBlob(stmt, 1, vKeep[i].first.data(), vKeep[i].first.size());
                        BindBlob(stmt, 2, vKeep[i].second.data(), vKeep[i].second.size());
                        fSuccess = sqlite3_step(stmt) == SQLITE_DONE;
                        sqlite3_reset(stmt);
                    }
                    sqlite3_finalize(stmt);
                    fSuccess = fSuccess && Exec(pfile->db, "COMMIT", strError);
                    if (!fSuccess) {
                        std::string strIgnored;
                        Exec(pfile->db, "ROLLBACK", strIgnored);
                    }
                } else if (fSuccess) {
                    // Write a fresh file, check it, swap it in. A fresh file
                    // carries no free pages with old content in them.
                    bitdb.CloseDb(strFile);
                    bitdb.mapFileUseCount.erase(strFile);
                    boost::filesystem::path pathFile = RealWalletPath(GetDataDir() / strFile);
                    boost::filesystem::path pathRes = pathFile.string() + ".rewrite";
                    fSuccess = CreateSQLiteWalletFile(pathRes, vKeep, strError) &&
                               VerifySQLiteWalletFile(pathRes, vKeep, strError);
                    if (fSuccess) {
                        try {
                            boost::filesystem::rename(pathRes, pathFile);
                            SyncDirectory(pathFile.parent_path());
                        } catch (const std::exception& e) {
                            strError = e.what();
                            fSuccess = false;
                        }
                    }
                    if (!fSuccess)
                        RemoveQuietly(pathRes);
                }
                if (!fSuccess)
                    LogPrintf("CDB::Rewrite : Failed to rewrite database file %s: %s\n", strFile, strError);
                return fSuccess;
            }
        }
        MilliSleep(100);
    }
    return false;
}
