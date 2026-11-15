// Copyright (c) 2015-2021 The Dogecoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/random/mersenne_twister.hpp>
#include <boost/random/uniform_int.hpp>

#include <arith_uint256.h>
#include <chain.h>
#include <dogecoin.h>
#include <logging.h>

#ifdef __clang__
__attribute__((no_sanitize("unsigned-integer-overflow")))
#endif // __clang__
int static generateMTRandom(unsigned int s, int range)
{
    boost::mt19937 gen(s);
    boost::uniform_int<> dist(1, range);
    return dist(gen);
}

CAmount GetDogecoinBlockSubsidy(int nHeight, const Consensus::Params& consensusParams, uint256 prevHash)
{
    // (BOB) reward curve, ported from dobbscoin-source GetBlockValue (subsidy only; the caller
    // adds nFees). This is the consensus cap: a coinbase paying <= this is valid (check is `>`).
    // VALIDATED 2026-11-15 vs real (BOB) coinbases across every segment of the curve (19/20 exact;
    // the one diff at 951753 is a legal miner underpayment, 0.625 <= cap 1.5). prevHash unused
    // ((BOB) never used Dogecoin's prev-hash random rewards).
    (void)prevHash;
    const bool testnet = consensusParams.fPowAllowMinDifficultyBlocks;
    CAmount nSubsidy = 50 * COIN;
    if (testnet && nHeight >= 350) nSubsidy = 10 * COIN;
    if (testnet && nHeight == 205) nSubsidy = 9 * COIN;
    if (!testnet && nHeight == 68425) nSubsidy = 10 * COIN;
    if (!testnet && nHeight >= 69791) nSubsidy = 10 * COIN;
    if (!testnet && nHeight <= 951752) nSubsidy >>= (nHeight / 210000);
    if (!testnet && nHeight >= 951753) nSubsidy = 150000000;   // 1.5 * COIN hardset
    if (nSubsidy <= 50000000) nSubsidy = 150000000;            // failsafe: <= 0.5 COIN -> 1.5 COIN
    return nSubsidy;
}


// Dogecoin: Normally minimum difficulty blocks can only occur in between
// retarget blocks. However, once we introduce Digishield every block is
// a retarget, so we need to handle minimum difficulty on all blocks.
bool AllowDigishieldMinDifficultyForBlock(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    // check if the chain allows minimum difficulty blocks
    if (!params.fPowAllowMinDifficultyBlocks)
        return false;

    // check if the chain allows minimum difficulty blocks on recalc blocks
    if (pindexLast->nHeight < 157500)
    // if (!params.fPowAllowDigishieldMinDifficultyBlocks)
        return false;

    // Allow for a minimum block time if the elapsed time > 2*nTargetSpacing
    return (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing*2);
}

unsigned int CalculateDogecoinNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params)
{
    if (params.fPowNoRetargeting)
        return pindexLast->nBits;

    int nHeight = pindexLast->nHeight + 1;
    bool fNewDifficultyProtocol = (nHeight >= 145000);
    // bool fNewDifficultyProtocol = (nHeight >= params.GetDigiShieldForkBlock());
    const int64_t nRetargetTimespan = fNewDifficultyProtocol
                             ? 60 // params.DigiShieldTargetTimespan()
                             : params.nPowTargetTimespan;

    int64_t nTimespan = pindexLast->GetBlockTime() - nFirstBlockTime;
    int64_t nMaxTimespan;
    int64_t nMinTimespan;

    if (fNewDifficultyProtocol) //DigiShield implementation - thanks to RealSolid & WDC for this code
    {
        // amplitude filter - thanks to daft27 for this code
        nTimespan = nRetargetTimespan + (nTimespan - nRetargetTimespan) / 8;

        nMinTimespan = nRetargetTimespan - (nRetargetTimespan / 4);
        nMaxTimespan = nRetargetTimespan + (nRetargetTimespan / 2);
    } else if (nHeight > 10000) {
        nMinTimespan = nRetargetTimespan / 4;
        nMaxTimespan = nRetargetTimespan * 4;
    } else if (nHeight > 5000) {
        nMinTimespan = nRetargetTimespan / 8;
        nMaxTimespan = nRetargetTimespan * 4;
    } else {
        nMinTimespan = nRetargetTimespan / 16;
        nMaxTimespan = nRetargetTimespan * 4;
    }

    // Limit adjustment step
    if (nTimespan < nMinTimespan)
        nTimespan = nMinTimespan;
    else if (nTimespan > nMaxTimespan)
        nTimespan = nMaxTimespan;

    // Retarget
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    arith_uint256 bnNew;
    bnNew.SetCompact(pindexLast->nBits);
    bnNew *= nTimespan;
    bnNew /= nRetargetTimespan;

    if (bnNew > bnPowLimit)
        bnNew = bnPowLimit;

    return bnNew.GetCompact();
}
