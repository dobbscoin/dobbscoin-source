// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow.h>

#include <arith_uint256.h>
#include <auxpow.h>
#include <util/system.h>
#include <chain.h>
#include <dogecoin.h>
#include <primitives/block.h>
#include <uint256.h>
#include <cmath>

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

static unsigned int BobGetNextWorkRequired_KGW(const CBlockIndex* pindexLast, const CBlockHeader* pblock, const Consensus::Params& params)
{
    // (BOB) KimotoGravityWell, ported WITHOUT OpenSSL (dobbscoin-source used CBigNum/OpenSSL BN).
    // KGW is a CLOSED historical regime (heights 13579..31596, buried ~1.86M deep): it only ever
    // validates already-mined blocks, so exact reproduction of real inputs is full correctness.
    // Big-int need met with arith_uint256 + an exact split mul/div: q*mul + (r*mul)/div keeps
    // every intermediate < 2^256 for all real historical inputs. Running average truncates toward
    // zero (BN_div semantics), via sign-branch to avoid unsigned underflow.
    // VALIDATED 2026-11-15 vs real (BOB) history: 97 consecutive heights, 0 mismatches (incl. negative deltas).
    const arith_uint256 powLimit = UintToArith256(params.powLimit);
    const int64_t TargetBlocksSpacing = 600;
    const uint64_t PastBlocksMin = 9;
    const uint64_t PastBlocksMax = 252;

    if (pindexLast == nullptr || pindexLast->nHeight == 0 || (uint64_t)pindexLast->nHeight < PastBlocksMin)
        return powLimit.GetCompact();

    const CBlockIndex* BlockReading = pindexLast;
    uint64_t PastBlocksMass = 0;
    int64_t PastRateActualSeconds = 0;
    int64_t PastRateTargetSeconds = 0;
    int64_t LatestBlockTime = pindexLast->GetBlockTime();
    arith_uint256 PastDifficultyAverage, PastDifficultyAveragePrev;

    for (unsigned int i = 1; BlockReading && BlockReading->nHeight > 0; i++) {
        if (PastBlocksMax > 0 && i > PastBlocksMax) break;
        PastBlocksMass++;
        arith_uint256 cur;
        cur.SetCompact(BlockReading->nBits);
        if (i == 1) {
            PastDifficultyAverage = cur;
        } else {
            const arith_uint256 denom = arith_uint256((uint64_t)i);
            if (cur >= PastDifficultyAveragePrev)
                PastDifficultyAverage = PastDifficultyAveragePrev + (cur - PastDifficultyAveragePrev) / denom;
            else
                PastDifficultyAverage = PastDifficultyAveragePrev - (PastDifficultyAveragePrev - cur) / denom;
        }
        PastDifficultyAveragePrev = PastDifficultyAverage;

        const int64_t rt = BlockReading->GetBlockTime();
        if (LatestBlockTime < rt) LatestBlockTime = rt;
        PastRateActualSeconds = LatestBlockTime - rt;
        PastRateTargetSeconds = TargetBlocksSpacing * (int64_t)PastBlocksMass;
        if (PastRateActualSeconds < 1) PastRateActualSeconds = 1;
        double PastRateAdjustmentRatio = 1.0;
        if (PastRateActualSeconds != 0 && PastRateTargetSeconds != 0)
            PastRateAdjustmentRatio = (double)PastRateTargetSeconds / (double)PastRateActualSeconds;
        const double EventHorizonDeviation = 1.0 + (0.7084 * std::pow((double)PastBlocksMass / 9.0, -1.228));
        if (PastBlocksMass >= PastBlocksMin) {
            if (PastRateAdjustmentRatio <= (1.0 / EventHorizonDeviation) ||
                PastRateAdjustmentRatio >= EventHorizonDeviation)
                break;
        }
        if (BlockReading->pprev == nullptr) break;
        BlockReading = BlockReading->pprev;
    }

    arith_uint256 bnNew = PastDifficultyAverage;
    if (PastRateActualSeconds != 0 && PastRateTargetSeconds != 0) {
        const arith_uint256 mul = arith_uint256((uint64_t)PastRateActualSeconds);
        const arith_uint256 div = arith_uint256((uint64_t)PastRateTargetSeconds);
        const arith_uint256 q = bnNew / div;
        const arith_uint256 r = bnNew - q * div;
        bnNew = q * mul + (r * mul) / div;     // exact floor(bnNew*mul/div)
    }
    if (bnNew > powLimit) bnNew = powLimit;
    return bnNew.GetCompact();
}

static unsigned int BobGetNextWorkRequired_LWMA3(const CBlockIndex* pindexLast, const CBlockHeader* pblock, const Consensus::Params& params)
{
    // (BOB) LWMA-3 (Zawy), the LIVE retarget regime (heights >= 1888808): per-block linearly
    // weighted moving average. Ported from dobbscoin-source GetNextWorkRequired_LWMA3.
    // Plain arith_uint256 (no bignum): target/(N*k) keeps terms small and sumTarget*weightedTime
    // stays < 2^256 for real inputs (scalars fit uint32). Unlike the closed regimes this one also
    // computes FUTURE blocks, so validated near the live tip. VALIDATED 2026-11-15 vs real (BOB):
    // 40 recent heights + the bootstrap->LWMA3 fork boundary (1888867/68/69), all exact.
    enum { N = 60 };
    const int64_t T = 120;
    const int64_t k = (int64_t(N) * (N + 1) * T) / 2;   // 219600
    const int64_t NK = int64_t(N) * k;                  // 13,176,000
    // HARDFORK_LWMA3_MAIN / HARDFORK_LWMA3_TESTNET: the bootstrap below counts from the
    // network's own fork height (testnet once inherited mainnet's and never retargeted)
    const int FORK = params.fPowAllowMinDifficultyBlocks ? 100 : 1888808;
    const arith_uint256 powLimit = UintToArith256(params.powLimit);

    if (pindexLast == nullptr) return powLimit.GetCompact();

    // Bootstrap: for the first N blocks past the fork, inherit the previous bits.
    if (pindexLast->nHeight + 1 < FORK + N)
        return pindexLast->nBits;

    // Walk back N+1 indices: blocks[N] = pindexLast, blocks[0] = pindexLast - N.
    const CBlockIndex* blocks[N + 1];
    blocks[N] = pindexLast;
    for (int i = N - 1; i >= 0; --i)
        blocks[i] = blocks[i + 1]->pprev;

    arith_uint256 sumTarget;
    int64_t weightedTime = 0;
    int64_t previousTime = blocks[0]->GetBlockTime();
    for (int64_t i = 1; i <= N; ++i) {
        int64_t thisTime = blocks[i]->GetBlockTime();
        int64_t solvetime = thisTime - previousTime;
        if (solvetime >  6 * T) solvetime =  6 * T;
        if (solvetime < -6 * T) solvetime = -6 * T;
        previousTime = thisTime;
        weightedTime += solvetime * i;
        arith_uint256 target;
        target.SetCompact(blocks[i]->nBits);
        sumTarget += target / arith_uint256((uint64_t)NK);
    }
    if (weightedTime < k / 3) weightedTime = k / 3;
    arith_uint256 nextTarget = sumTarget * arith_uint256((uint64_t)weightedTime);
    if (nextTarget > powLimit) nextTarget = powLimit;
    return nextTarget.GetCompact();
}

bool BobIsEmergencyDifficultyBlock(const CBlockHeader& block, const CBlockIndex* pindexPrev, const Consensus::Params& params)
{
    // (BOB) emergency-difficulty valve (dobbscoin-source IsEmergencyDifficultyBlock,
    // HARDFORK_EMERGENCY_DIFF_MAIN=1888888). A block that (a) is past the activation
    // height, (b) follows a stall of strictly more than 6h, and (c) carries exactly
    // powLimit, is accepted even though the retarget algorithm would demand a harder
    // target -- it lets the chain unstick after a hashrate-departure event. This is a
    // VALIDATION EXEMPTION, not a change to GetNextWorkRequired: a stalled block may
    // instead carry the normal (stuck) target and still pass the ordinary nBits check.
    if (pindexPrev == nullptr) return false;
    const int forkHeight = params.fPowAllowMinDifficultyBlocks ? 20 : 1888888; // EMERGENCY_DIFF {TESTNET,MAIN}
    if (pindexPrev->nHeight + 1 < forkHeight) return false;
    const int64_t gap = (int64_t)block.nTime - (int64_t)pindexPrev->GetBlockTime();
    if (gap <= 6 * 60 * 60) return false;                          // EMERGENCY_DIFFICULTY_GAP = 21600s, strict >
    return block.nBits == UintToArith256(params.powLimit).GetCompact();   // (c) must claim min difficulty
}

unsigned int BobGetNextWorkRequiredForMining(const CBlockIndex* pindexLast, const CBlockHeader* pblock, const Consensus::Params& params)
{
    // Same predicate as the validation exemption, so the two can never disagree about
    // when the window is open: the header as it would be at powLimit.
    CBlockHeader at_limit = *pblock;
    at_limit.nBits = UintToArith256(params.powLimit).GetCompact();
    if (BobIsEmergencyDifficultyBlock(at_limit, pindexLast, params))
        return at_limit.nBits;
    return GetNextWorkRequired(pindexLast, pblock, params);
}

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    assert(pindexLast != nullptr);
    unsigned int nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();

    // --- (BOB) retarget dispatcher (by height) ---
    // All five (BOB) regimes, each ported and validated against real history: V1 (<13579),
    // KGW [13579,31597), DigiShield V3 [31597,68425) and V4 [68425,1888808), LWMA-3 (>=1888808).
    // Unit vectors from real mainnet headers: src/test/dogecoin_tests.cpp (bob_retarget_*).
    if (!params.fPowNoRetargeting && params.fPowAllowMinDifficultyBlocks) {
        // (BOB) testnet, as 0.14 GetNextWorkRequired's AllowMinDifficultyBlocks branch: V1 below
        // 20, KGW from 20, DigiShield (10-minute target) from 50, LWMA-3 from 100
        // (HARDFORK_LWMA3_TESTNET; 0.14's mode-4 switch at 200 is dead code, LWMA-3 wins first).
        const int h = pindexLast->nHeight + 1;
        if (h < 20)  return BobGetNextWorkRequired_V1(pindexLast, pblock, params);
        if (h < 50)  return BobGetNextWorkRequired_KGW(pindexLast, pblock, params);
        if (h < 100) return BobGetNextWorkRequired_DigiShield(pindexLast, pblock, params, 600);
        return BobGetNextWorkRequired_LWMA3(pindexLast, pblock, params);
    }
    if (!params.fPowNoRetargeting) {
        const int h = pindexLast->nHeight + 1;
        if (h < 13579)                    return BobGetNextWorkRequired_V1(pindexLast, pblock, params);
        if (h >= 13579 && h < 31597)      return BobGetNextWorkRequired_KGW(pindexLast, pblock, params);
        if (h >= 31597 && h < 68425)      return BobGetNextWorkRequired_DigiShield(pindexLast, pblock, params, 600);
        if (h >= 68425 && h < 1888808)    return BobGetNextWorkRequired_DigiShield(pindexLast, pblock, params, 120);
        if (h >= 1888808)                 return BobGetNextWorkRequired_LWMA3(pindexLast, pblock, params);
        // All five (BOB) retarget regimes are now covered above; the stock Dogecoin retarget
        // below is only reached under fPowNoRetargeting (regtest).
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

bool BobCheckAuxPowProofOfWork(const CBlockHeader& block, const Consensus::Params& params)
{
    if (block.IsAuxpow()) {
        if (!block.auxpow)
            return error("%s: VERSION_AUXPOW set but no auxpow attached", __func__);
        if (!block.auxpow->check(block.GetHash(), params.nAuxpowChainId, params))
            return error("%s: auxpow commitment chain failed", __func__);
        return CheckProofOfWork(block.auxpow->getParentBlockPoWHash(), block.nBits, params);
    }
    return CheckProofOfWork(block.GetPoWHash(), block.nBits, params);
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
