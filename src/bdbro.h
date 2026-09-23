// Copyright (c) 2024 The Bitcoin Core developers
// Copyright (c) 2026 The Dobbscoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DOBBSCOIN_BDBRO_H
#define DOBBSCOIN_BDBRO_H

/**
 * A read-only parser for the Berkeley DB btree files that wallet.dat used to be.
 *
 * It lets this build read (and migrate) a wallet written by any earlier release
 * without linking Berkeley DB. Modeled on Bitcoin Core v28's
 * src/wallet/migrate.cpp (BerkeleyRODatabase, MIT licensed), rewritten for this
 * tree without its stream classes. The layout it understands is exactly what
 * CDB wrote: one file, an outer database holding a single subdatabase called
 * "main", a btree of unique keys, no BDB-level encryption, no duplicates.
 */

#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <boost/filesystem/path.hpp>

namespace BerkeleyRO
{
typedef std::vector<unsigned char> Bytes;
typedef std::map<Bytes, Bytes> RecordMap;
typedef std::vector<std::pair<Bytes, Bytes> > RecordList;

/** True when the file starts with a Berkeley DB btree metadata page (either byte order). */
bool IsBerkeleyBtreeFile(const boost::filesystem::path& path);

/**
 * Read every live record of the "main" subdatabase. Strict: any structural
 * inconsistency, an unflushed file (page LSNs not reset), a duplicate key or a
 * truncated file throws std::runtime_error with the reason. Never writes.
 */
void ReadAll(const boost::filesystem::path& path, RecordMap& records);

/**
 * Best-effort read for -salvagewallet. Tries ReadAll first; if that fails,
 * scans every page of the file and keeps each btree leaf page that parses on
 * its own, ignoring the tree structure. Records whose key was already seen are
 * dropped (first one wins). Returns false if nothing at all could be read.
 * strReport describes what was skipped.
 */
bool Salvage(const boost::filesystem::path& path, RecordList& records, std::string& strReport);
}

#endif // DOBBSCOIN_BDBRO_H
