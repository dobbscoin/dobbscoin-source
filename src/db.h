// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2026 The Dobbscoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DOBBSCOIN_DB_H
#define DOBBSCOIN_DB_H

#include "clientversion.h"
#include "serialize.h"
#include "streams.h"
#include "sync.h"
#include "version.h"

#include <map>
#include <string>
#include <vector>

#include <boost/filesystem/path.hpp>

/**
 * Wallet storage.
 *
 * Until v0.13.x the wallet lived in a Berkeley DB 4.8 btree. It now lives in a
 * single SQLite file with one table, main(key BLOB PRIMARY KEY, value BLOB),
 * holding the very same serialized key/value records CWalletDB always wrote.
 * Nothing about the records changes; only the container does. A Berkeley DB
 * wallet.dat found at startup is converted once by CDBEnv::MigrateFromBerkeley,
 * which reads it with the libdb-free parser in bdbro.h and keeps the original.
 */

class CDiskBlockIndex;
class COutPoint;
class CSQLiteFile;

struct CBlockLocator;

extern unsigned int nWalletDBUpdated;

void ThreadFlushWalletDB(const std::string& strWalletFile);

/** Version string of the wallet storage library, for the startup log and the debug window. */
std::string WalletDBVersion();

class CDBEnv
{
private:
    bool fDbEnvInit;
    bool fMockDb;
    boost::filesystem::path path;

public:
    mutable CCriticalSection cs_db;
    std::map<std::string, int> mapFileUseCount;
    std::map<std::string, CSQLiteFile*> mapDb;

    CDBEnv();
    ~CDBEnv();
    /** In-memory databases, for the unit tests. */
    void MakeMock();
    bool IsMock() { return fMockDb; }

    /**
     * Check that database file strFile is a readable SQLite wallet
     * (PRAGMA quick_check). Must be called BEFORE strFile is opened.
     */
    enum VerifyResult { VERIFY_OK,
                        RECOVER_OK,
                        RECOVER_FAIL };
    VerifyResult Verify(const std::string& strFile, std::string& strError);

    typedef std::pair<std::vector<unsigned char>, std::vector<unsigned char> > KeyValPair;

    /**
     * Replace the (closed) file strFile with a new SQLite file holding exactly
     * vRecords. The new file is written beside it, read back and compared
     * byte for byte, the original is preserved as "<strFile>.<tag>-<unixtime>"
     * (a hard link to the original inode, or a verified copy where links are
     * unsupported) and only then is the new file renamed over strFile. On any
     * failure strFile is left untouched and false is returned with strError set.
     */
    bool ReplaceWithSQLite(const std::string& strFile, const std::vector<KeyValPair>& vRecords,
                           const std::string& strBackupTag, std::string& strBackupName, std::string& strError);

    /**
     * If strFile is a Berkeley DB wallet, read it and migrate it to SQLite.
     * Returns true if there was nothing to do or the migration succeeded.
     */
    bool MigrateFromBerkeley(const std::string& strFile, std::string& strError);

    /**
     * Consistent copy of strFile to pathDest through the SQLite backup API,
     * written to "<pathDest>.tmp", compared record for record with the
     * wallet, then renamed into place.
     */
    bool Backup(const std::string& strFile, const boost::filesystem::path& pathDest, std::string& strError);

    bool Open(const boost::filesystem::path& path);
    void Close();
    void Flush(bool fShutdown);
    void CheckpointLSN(const std::string& strFile) {} // Berkeley DB leftover: SQLite commits are self-contained

    void CloseDb(const std::string& strFile);
    bool RemoveDb(const std::string& strFile);

    /** Open (or return the already open) connection for strFile. Caller holds cs_db. */
    CSQLiteFile* OpenFile(const std::string& strFile, bool fCreate);
};

extern CDBEnv bitdb;

/** File-level helpers, used by the migration and exposed for the unit tests. */
/** Create a new SQLite wallet file holding exactly vRecords (one transaction). */
bool CreateSQLiteWalletFile(const boost::filesystem::path& path, const std::vector<CDBEnv::KeyValPair>& vRecords, std::string& strError);
/** Every record of an SQLite wallet file, in key order. */
bool ReadSQLiteWalletFile(const boost::filesystem::path& path, std::vector<CDBEnv::KeyValPair>& vRecords, std::string& strError);
/** True only if the file holds exactly vExpected, byte for byte, and passes quick_check. */
bool VerifySQLiteWalletFile(const boost::filesystem::path& path, std::vector<CDBEnv::KeyValPair> vExpected, std::string& strError);

/** A forward-only, key-ordered cursor over one wallet file. */
class CDBCursor;

enum {
    DB_CURSOR_OK = 0,
    DB_CURSOR_DONE = 1,
    DB_CURSOR_ERROR = -1
};

/** RAII class that provides access to a wallet database */
class CDB
{
protected:
    CSQLiteFile* pdb;
    std::string strFile;
    bool activeTxn;
    bool fReadOnly;

    explicit CDB(const std::string& strFilename, const char* pszMode = "r+");
    ~CDB() { Close(); }

public:
    void Flush();
    void Close();

private:
    CDB(const CDB&);
    void operator=(const CDB&);

    bool ReadRaw(const CDataStream& ssKey, CDataStream& ssValue);
    bool WriteRaw(const CDataStream& ssKey, const CDataStream& ssValue, bool fOverwrite);
    bool EraseRaw(const CDataStream& ssKey);
    bool ExistsRaw(const CDataStream& ssKey);

protected:
    template <typename K, typename T>
    bool Read(const K& key, T& value)
    {
        if (!pdb)
            return false;

        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        ssKey.reserve(1000);
        ssKey << key;
        CDataStream ssValue(SER_DISK, CLIENT_VERSION);
        bool fFound = ReadRaw(ssKey, ssValue);
        memory_cleanse_stream(ssKey);
        if (!fFound)
            return false;

        bool fOk = true;
        try {
            ssValue >> value;
        } catch (const std::exception&) {
            fOk = false;
        }
        memory_cleanse_stream(ssValue);
        return fOk;
    }

    template <typename K, typename T>
    bool Write(const K& key, const T& value, bool fOverwrite = true)
    {
        if (!pdb)
            return false;
        if (fReadOnly)
            assert(!"Write called on database in read-only mode");

        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        ssKey.reserve(1000);
        ssKey << key;
        CDataStream ssValue(SER_DISK, CLIENT_VERSION);
        ssValue.reserve(10000);
        ssValue << value;

        bool fOk = WriteRaw(ssKey, ssValue, fOverwrite);

        // Clear memory in case it was a private key
        memory_cleanse_stream(ssKey);
        memory_cleanse_stream(ssValue);
        return fOk;
    }

    template <typename K>
    bool Erase(const K& key)
    {
        if (!pdb)
            return false;
        if (fReadOnly)
            assert(!"Erase called on database in read-only mode");

        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        ssKey.reserve(1000);
        ssKey << key;
        bool fOk = EraseRaw(ssKey);
        memory_cleanse_stream(ssKey);
        return fOk;
    }

    template <typename K>
    bool Exists(const K& key)
    {
        if (!pdb)
            return false;

        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        ssKey.reserve(1000);
        ssKey << key;
        bool fOk = ExistsRaw(ssKey);
        memory_cleanse_stream(ssKey);
        return fOk;
    }

    static void memory_cleanse_stream(CDataStream& ss)
    {
        if (!ss.empty())
            memset(&ss[0], 0, ss.size());
    }

    /**
     * Cursor over every record in key order, or (with pssStart) over every
     * record whose key is >= *pssStart -- Berkeley DB's DB_SET_RANGE.
     * Returns NULL on error. Free with CloseCursor.
     */
    CDBCursor* GetCursor(const CDataStream* pssStart = NULL);
    /** DB_CURSOR_OK with ssKey/ssValue filled, DB_CURSOR_DONE past the end, DB_CURSOR_ERROR. */
    int ReadAtCursor(CDBCursor* pcursor, CDataStream& ssKey, CDataStream& ssValue);
    void CloseCursor(CDBCursor* pcursor);

public:
    bool TxnBegin();
    bool TxnCommit();
    bool TxnAbort();

    bool ReadVersion(int& nVersion)
    {
        nVersion = 0;
        return Read(std::string("version"), nVersion);
    }

    bool WriteVersion(int nVersion)
    {
        return Write(std::string("version"), nVersion);
    }

    bool static Rewrite(const std::string& strFile, const char* pszSkip = NULL);
};

#endif // DOBBSCOIN_DB_H
