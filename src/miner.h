// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2013 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DOBBSCOIN_MINER_H
#define DOBBSCOIN_MINER_H

#include "crypto/scrypt_nway.h"

#include <atomic>
#include <stdint.h>
#include <string>

class CBlock;
class CBlockHeader;
class CBlockIndex;
class CReserveKey;
class CScript;
class CWallet;

struct CBlockTemplate;

/** Run the miner threads */
void GenerateDobbscoins(bool fGenerate, CWallet* pwallet, int nThreads);
/** Generate a new block, without valid proof-of-work */
CBlockTemplate* CreateNewBlock(const CScript& scriptPubKeyIn);
CBlockTemplate* CreateNewBlockWithKey(CReserveKey& reservekey);
/** Modify the extranonce in a block */
void IncrementExtraNonce(CBlock* pblock, CBlockIndex* pindexPrev, unsigned int& nExtraNonce);
/** Check mined block */
bool CheckWork(CBlock* pblock, CWallet& wallet, CReserveKey& reservekey);
void UpdateTime(CBlockHeader* block, const CBlockIndex* pindexPrev);

/**
 * Pick the scrypt implementation the internal miner hashes with, and log it:
 * "auto" or "" for the fastest this CPU supports, else generic, sse2, avx or
 * avx2 (-minerscrypt). A name the CPU cannot run falls back to the fastest one.
 * Only the miner is affected: block validation always uses generic scrypt.
 */
void MinerScryptSelect(const std::string& strRequested);
/** The implementation the miner uses now, by name ("avx2" ...). */
std::string MinerScryptImplName();
/** The implementation the miners (solo and stratum) should hash with now. */
ScryptImpl MinerScryptImpl();
/**
 * Recompute hash32, which impl claimed for header80, with the generic scrypt.
 * True if they agree (always, for GENERIC). If not: logs it loudly, sets the
 * GUI/getinfo warning, switches the miners to GENERIC for the rest of the
 * session, and returns false -- the caller must then not submit.
 */
bool MinerScryptCheck(ScryptImpl impl, const unsigned char* header80, const unsigned char* hash32);

// Written by the miner threads, read by RPC and the GUI: atomic.
extern std::atomic<double> dHashesPerSec;
extern std::atomic<int64_t> nHPSTimerStart;

#endif // DOBBSCOIN_MINER_H
