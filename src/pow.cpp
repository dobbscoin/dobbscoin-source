// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow.h>

#include <arith_uint256.h>
#include <chain.h>
#include <dogecoin.h>
#include <primitives/block.h>
#include <uint256.h>

static unsigned int BobGetNextWorkRequired_V1(const CBlockIndex* pindexLast, const CBlockHeader* pblock, const Consensus::Params& params)
{
    // (BOB) V1 retarget: original Bitcoin-style, 2016-block interval, 14-day timespan.
    // Ported from dobbscoin-source pow.cpp GetNextWorkRequired_V1.
    // VALIDATED 2026-11-14 against real (BOB) retargets at heights 2016/4032/6048/8064/10080/12096 (6/6 exact).
    const arith_uint256 powLimit = UintToArith256(params.powLimit);
    const unsigned int nProofOfWorkLimit = powLimit.GetCompact();
    const int64_t nTargetTimespan = 14 * 24 * 60 * 60; // 1209600
    const int64_t nTargetSpacing  = 10 * 60;           // 600
    const int64_t nInterval       = nTargetTimespan / nTargetSpacing; // 2016

    if (pindexLast == nullptr) return nProofOfWorkLimit;

    if ((pindexLast->nHeight + 1) % nInterval != 0) {
        if (params.fPowAllowMinDifficultyBlocks) {
            if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + nTargetSpacing * 2)
                return nProofOfWorkLimit;
            const CBlockIndex* pindex = pindexLast;
            while (pindex->pprev && pindex->nHeight % nInterval != 0 && pindex->nBits == nProofOfWorkLimit)
                pindex = pindex->pprev;
            return pindex->nBits;
        }
        return pindexLast->nBits;
    }

    // Go back nInterval-1 blocks (the classic Bitcoin off-by-one, as (BOB) V1 does).
    const CBlockIndex* pindexFirst = pindexLast;
    for (int i = 0; pindexFirst && i < nInterval - 1; i++)
        pindexFirst = pindexFirst->pprev;
    assert(pindexFirst);

    int64_t nActualTimespan = pindexLast->GetBlockTime() - pindexFirst->GetBlockTime();
    if (nActualTimespan < nTargetTimespan / 4) nActualTimespan = nTargetTimespan / 4;
    if (nActualTimespan > nTargetTimespan * 4) nActualTimespan = nTargetTimespan * 4;

    arith_uint256 bnNew;
    bnNew.SetCompact(pindexLast->nBits);
    bnNew *= (uint32_t)nActualTimespan;                 // wraps mod 2^256 (matches validated math)
    bnNew /= arith_uint256((uint64_t)nTargetTimespan);
    if (bnNew > powLimit) bnNew = powLimit;
    return bnNew.GetCompact();
}

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    assert(pindexLast != nullptr);
    unsigned int nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();

    // --- (BOB) retarget dispatcher ---
    // V1 ported + validated against real history (heights < 13579). KGW/DigiShield/LWMA3
    // for heights >= 13579 are NOT yet ported (see doc/retarget-port-spec.md); the stock
    // Dogecoin retarget below stands in as a PLACEHOLDER for those heights only.
    if (!params.fPowNoRetargeting && (pindexLast->nHeight + 1) < 13579)
        return BobGetNextWorkRequired_V1(pindexLast, pblock, params);

    // Dogecoin: Special rules for minimum difficulty blocks with Digishield
    if (AllowDigishieldMinDifficultyForBlock(pindexLast, pblock, params))
    {
        // Special difficulty rule for testnet:
        // If the new block's timestamp is more than 2* nTargetSpacing minutes
        // then allow mining of a min-difficulty block.
        return nProofOfWorkLimit;
    }

    // Only change once per difficulty adjustment interval
    bool fNewDifficultyProtocol = (pindexLast->nHeight+1 >= 145000);
    const int64_t difficultyAdjustmentInterval = fNewDifficultyProtocol
                                                 ? 1
                                                 : params.DifficultyAdjustmentInterval();
    const int64_t powTargetSpacing = fNewDifficultyProtocol
                                                 ? 60
                                                 : params.nPowTargetSpacing;

    if ((pindexLast->nHeight+1) % difficultyAdjustmentInterval != 0)
    {
        if (params.fPowAllowMinDifficultyBlocks)
        {
            // Special difficulty rule for testnet:
            // If the new block's timestamp is more than 2* 10 minutes
            // then allow mining of a min-difficulty block.
            if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + powTargetSpacing*2) {
                return nProofOfWorkLimit;
            }
            else
            {
                // Return the last non-special-min-difficulty-rules-block
                const CBlockIndex* pindex = pindexLast;
                while (pindex->pprev && pindex->nHeight % difficultyAdjustmentInterval != 0 && pindex->nBits == nProofOfWorkLimit)
                    pindex = pindex->pprev;
                return pindex->nBits;
            }
        }
        return pindexLast->nBits;
    }

    // Litecoin: This fixes an issue where a 51% attack can change difficulty at will.
    // Go back the full period unless it's the first retarget after genesis. Code courtesy of Art Forz
    int blockstogoback = difficultyAdjustmentInterval-1;
    if ((pindexLast->nHeight+1) != difficultyAdjustmentInterval)
        blockstogoback = difficultyAdjustmentInterval;

    // Go back by what we want to be 14 days worth of blocks
    int nHeightFirst = pindexLast->nHeight - blockstogoback;
    assert(nHeightFirst >= 0);
    const CBlockIndex* pindexFirst = pindexLast->GetAncestor(nHeightFirst);
    assert(pindexFirst);

    return CalculateDogecoinNextWorkRequired(pindexLast, pindexFirst->GetBlockTime(), params);
}

unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params)
{
    if (params.fPowNoRetargeting)
        return pindexLast->nBits;

    // Limit adjustment step
    int64_t nActualTimespan = pindexLast->GetBlockTime() - nFirstBlockTime;
    if (nActualTimespan < params.nPowTargetTimespan/4)
        nActualTimespan = params.nPowTargetTimespan/4;
    if (nActualTimespan > params.nPowTargetTimespan*4)
        nActualTimespan = params.nPowTargetTimespan*4;

    // Retarget
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    arith_uint256 bnNew;
    bnNew.SetCompact(pindexLast->nBits);
    bnNew *= nActualTimespan;
    bnNew /= params.nPowTargetTimespan;

    if (bnNew > bnPowLimit)
        bnNew = bnPowLimit;

    return bnNew.GetCompact();
}

bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    // (BOB)/Dobbscoin genesis is PoW-exempt, carried from dobbscoin-source pow.cpp
    // (HASHGENESISBLOCKPOW). Its genesis header does not satisfy the target; (BOB)
    // bypasses the check for exactly this one scrypt pow-hash.
    if (hash == uint256S("0xb83bb8028229439a4e645a56d48ee31b884efdb0e395c8538a10a462166563f3"))
        return true;
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(params.powLimit))
        return false;

    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget)
        return false;

    return true;
}
