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

static unsigned int BobGetNextWorkRequired_DigiShield(const CBlockIndex* pindexLast, const CBlockHeader* pblock, const Consensus::Params& params, int64_t T)
{
    // (BOB) DigiShield: per-block retarget (interval=1), amplitude filter, clamp [0.75T, 1.5T].
    // Ported from dobbscoin-source GetNextWorkRequired_V3 (T=600, 10-min) and _V4 (T=120, 2-min).
    // (BOB)'s interval=1 makes the min-difficulty branch dead code, so it is omitted here.
    // VALIDATED 2026-11-15 vs real (BOB) history across V3, V4 and both regime boundaries (23/23).
    const arith_uint256 powLimit = UintToArith256(params.powLimit);
    const unsigned int nProofOfWorkLimit = powLimit.GetCompact();
    if (pindexLast == nullptr) return nProofOfWorkLimit;

    const CBlockIndex* pindexFirst = pindexLast->pprev;   // blockstogoback == 1
    assert(pindexFirst);

    int64_t nActualTimespan = pindexLast->GetBlockTime() - pindexFirst->GetBlockTime();
    nActualTimespan = T + (nActualTimespan - T) / 8;       // amplitude filter (C truncates toward 0)
    if (nActualTimespan < (T - T / 4)) nActualTimespan = T - T / 4;
    if (nActualTimespan > (T + T / 2)) nActualTimespan = T + T / 2;

    arith_uint256 bnNew;
    bnNew.SetCompact(pindexLast->nBits);
    bnNew *= (uint32_t)nActualTimespan;
    bnNew /= arith_uint256((uint64_t)T);
    if (bnNew > powLimit) bnNew = powLimit;
    return bnNew.GetCompact();
}

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    assert(pindexLast != nullptr);
    unsigned int nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();

    // --- (BOB) retarget dispatcher (by height) ---
    // Ported + validated vs real history: V1 (<13579), DigiShield V3 [31597,68425), V4 [68425,1888808).
    // NOT yet ported: KGW [13579,31597) (portable/no-OpenSSL port pending) and LWMA-3 (>=1888808).
    // The stock Dogecoin retarget below is a PLACEHOLDER for those two ranges only. See doc/retarget-port-spec.md.
    if (!params.fPowNoRetargeting) {
        const int h = pindexLast->nHeight + 1;
        if (h < 13579)                    return BobGetNextWorkRequired_V1(pindexLast, pblock, params);
        if (h >= 31597 && h < 68425)      return BobGetNextWorkRequired_DigiShield(pindexLast, pblock, params, 600);
        if (h >= 68425 && h < 1888808)    return BobGetNextWorkRequired_DigiShield(pindexLast, pblock, params, 120);
        // h in [13579,31597) -> KGW (TODO), h >= 1888808 -> LWMA-3 (TODO): fall through to placeholder.
    }

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
