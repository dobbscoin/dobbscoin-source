// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_POW_H
#define BITCOIN_POW_H

#include <consensus/params.h>

#include <stdint.h>

class CBlockHeader;
class CBlockIndex;
class uint256;

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params&);
bool BobIsEmergencyDifficultyBlock(const CBlockHeader& block, const CBlockIndex* pindexPrev, const Consensus::Params& params);
/** (BOB) mining-side valve (0.14 v0.13.3 GetNextWorkRequiredForMining): once the tip is more than
 *  6h old (past the activation height) a template claims powLimit, so the valve can actually fire.
 *  Miner policy only; validation is unchanged (BobIsEmergencyDifficultyBlock). */
unsigned int BobGetNextWorkRequiredForMining(const CBlockIndex* pindexLast, const CBlockHeader* pblock, const Consensus::Params& params);
unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params&);

/** Check whether a block hash satisfies the proof-of-work requirement specified by nBits */
bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params&);

class CBlockHeader;
/**
 * (BOB) 0.14 CheckAuxPowProofOfWork: with VERSION_AUXPOW set, the auxpow must
 * be attached, its commitment chain must check against our chain ID, and the
 * PARENT block's scrypt hash must meet nBits.  Otherwise the header's own
 * scrypt hash must.  Height rules live in ContextualCheckBlockHeader.
 */
bool BobCheckAuxPowProofOfWork(const CBlockHeader& block, const Consensus::Params&);

#endif // BITCOIN_POW_H
