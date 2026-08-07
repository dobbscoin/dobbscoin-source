// Copyright (c) 2026 The Dobbscoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chain.h"
#include "chainparams.h"
#include "chainparamsbase.h"
#include "pow.h"
#include "primitives/block.h"
#include "uint256.h"

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(emergency_diff_tests)

namespace {

// Build the minimum fixture IsEmergencyDifficultyBlock reads: a CBlockIndex
// with nHeight (gates activation) and nTime (gates the >6h gap check).
static CBlockIndex MakePrev(int height, uint32_t nTime)
{
    CBlockIndex idx;
    idx.SetNull();
    idx.nHeight = height;
    idx.nTime   = nTime;
    return idx;
}

static unsigned int PowLimitCompact()
{
    return Params().ProofOfWorkLimit().GetCompact();
}

} // anon

// Pre-activation: a block claiming min-difficulty after a 24h stall is NOT
// granted the emergency exemption. The rule is height-gated; v0.12.0 and
// earlier nodes must reject such a block as bad-diffbits.
BOOST_AUTO_TEST_CASE(emergency_diff_inactive_below_fork)
{
    SelectParams(CBaseChainParams::MAIN);

    // pindexPrev->nHeight + 1 == HARDFORK_EMERGENCY_DIFF_MAIN - 1, strictly
    // below the >= fork threshold.
    CBlockIndex prev = MakePrev(HARDFORK_EMERGENCY_DIFF_MAIN - 2, 1800000000u);

    CBlockHeader header;
    header.nTime = prev.nTime + 24 * 60 * 60;  // 24h gap — well past the 6h floor
    header.nBits = PowLimitCompact();

    BOOST_CHECK(!IsEmergencyDifficultyBlock(header, &prev));

    SelectParams(CBaseChainParams::UNITTEST);
}

// Post-fork, normal-cadence block at min-difficulty — REJECTED. A short
// solvetime cannot be paired with min-difficulty bits just because the
// activation height has been crossed. The 6h gap is load-bearing.
BOOST_AUTO_TEST_CASE(emergency_diff_normal_block_at_min_diff_rejected)
{
    SelectParams(CBaseChainParams::MAIN);

    CBlockIndex prev = MakePrev(HARDFORK_EMERGENCY_DIFF_MAIN - 1, 1800000000u);

    CBlockHeader header;
    header.nTime = prev.nTime + 120u;          // T = 2 min, healthy chain
    header.nBits = PowLimitCompact();

    BOOST_CHECK(!IsEmergencyDifficultyBlock(header, &prev));

    SelectParams(CBaseChainParams::UNITTEST);
}

// Post-fork, 6h+ gap with min-difficulty bits — ACCEPTED. This is the
// canonical recovery path: hashrate departed, chain stalled, the first
// miner to publish anything at all at powLimit gets it accepted.
BOOST_AUTO_TEST_CASE(emergency_diff_long_gap_at_min_diff_accepted)
{
    SelectParams(CBaseChainParams::MAIN);

    CBlockIndex prev = MakePrev(HARDFORK_EMERGENCY_DIFF_MAIN - 1, 1800000000u);

    CBlockHeader header;
    header.nTime = prev.nTime + EMERGENCY_DIFFICULTY_GAP + 1;  // 6h + 1s
    header.nBits = PowLimitCompact();

    BOOST_CHECK(IsEmergencyDifficultyBlock(header, &prev));

    SelectParams(CBaseChainParams::UNITTEST);
}

// Post-fork, 6h+ gap but the miner found work at BETTER than min-difficulty —
// the emergency exemption does NOT apply. The block must validate under the
// normal nBits == GetNextWorkRequired() path. (The exemption is only a
// "safety valve"; a miner who can do real work after the stall is held to
// the real target.)
BOOST_AUTO_TEST_CASE(emergency_diff_long_gap_better_than_min_not_emergency)
{
    SelectParams(CBaseChainParams::MAIN);

    CBlockIndex prev = MakePrev(HARDFORK_EMERGENCY_DIFF_MAIN - 1, 1800000000u);

    // Build a target one mantissa-bit tighter than powLimit — definitely
    // != powLimit.GetCompact(), but still trivially mineable in the test
    // harness sense (we don't actually hash here).
    uint256 powLimit = Params().ProofOfWorkLimit();
    uint256 tighter  = powLimit >> 1;            // 2× harder than powLimit
    unsigned int tighterBits = tighter.GetCompact();
    BOOST_REQUIRE(tighterBits != PowLimitCompact());

    CBlockHeader header;
    header.nTime = prev.nTime + EMERGENCY_DIFFICULTY_GAP + 100;
    header.nBits = tighterBits;

    BOOST_CHECK(!IsEmergencyDifficultyBlock(header, &prev));

    SelectParams(CBaseChainParams::UNITTEST);
}

// Edge: exactly 6h. The predicate is strict-greater (`gap > 21600`), so
// 21600s on the nose is not enough. One second short of qualifying.
BOOST_AUTO_TEST_CASE(emergency_diff_exact_6h_gap_rejected)
{
    SelectParams(CBaseChainParams::MAIN);

    CBlockIndex prev = MakePrev(HARDFORK_EMERGENCY_DIFF_MAIN - 1, 1800000000u);

    CBlockHeader header;
    header.nTime = prev.nTime + EMERGENCY_DIFFICULTY_GAP;  // exactly 6h
    header.nBits = PowLimitCompact();

    BOOST_CHECK(!IsEmergencyDifficultyBlock(header, &prev));

    SelectParams(CBaseChainParams::UNITTEST);
}

// Defensive: pindexPrev == NULL should never crash and must return false.
// In live code this can't happen (ContextualCheckBlockHeader asserts prev)
// but the helper is exposed publicly and the guard is cheap.
BOOST_AUTO_TEST_CASE(emergency_diff_null_prev_safe)
{
    SelectParams(CBaseChainParams::MAIN);

    CBlockHeader header;
    header.nTime = 1800000000u;
    header.nBits = PowLimitCompact();

    BOOST_CHECK(!IsEmergencyDifficultyBlock(header, NULL));

    SelectParams(CBaseChainParams::UNITTEST);
}

// ---------------------------------------------------------------------------
// Mining-side valve (v0.13.3). The v0.13.0 tests above prove the ACCEPTANCE
// predicate; none of them proved a template ever PRODUCES a qualifying block
// — and in v0.13.0–v0.13.2 none did (the valve was validation-only, same
// class of bug as OFF's pre-Nodens "welded valve"). These pin the chooser
// the miner actually calls.
// ---------------------------------------------------------------------------

// Post-fork, tip gap > 6h: the mining chooser must return min-difficulty so
// the produced block satisfies IsEmergencyDifficultyBlock. This is the
// un-weld itself — v0.13.2 returns the retarget value here and can never
// fire the valve.
BOOST_AUTO_TEST_CASE(miner_valve_fires_post_fork_after_stall)
{
    SelectParams(CBaseChainParams::MAIN);

    CBlockIndex prev = MakePrev(HARDFORK_EMERGENCY_DIFF_MAIN - 1, 1800000000u);

    CBlockHeader header;
    header.nTime = prev.nTime + 7 * 60 * 60;  // 7h gap

    unsigned int nBits = GetNextWorkRequiredForMining(&prev, &header);
    BOOST_CHECK_EQUAL(nBits, PowLimitCompact());

    // And the template it yields is exactly what validation exempts.
    header.nBits = nBits;
    BOOST_CHECK(IsEmergencyDifficultyBlock(header, &prev));

    SelectParams(CBaseChainParams::UNITTEST);
}

// Below the activation height the chooser must NOT hand out min-difficulty,
// no matter how long the stall. (Height chosen inside LWMA-3's bootstrap
// window so the fallthrough is the deterministic reuse-prev-bits path.)
BOOST_AUTO_TEST_CASE(miner_valve_gated_below_fork)
{
    SelectParams(CBaseChainParams::MAIN);

    CBlockIndex prev = MakePrev(HARDFORK_LWMA3_MAIN + 10, 1800000000u);
    prev.nBits = 0x1b0404cb;  // arbitrary mid-difficulty, != powLimit

    CBlockHeader header;
    header.nTime = prev.nTime + 24 * 60 * 60;  // 24h stall, pre-activation

    unsigned int nBits = GetNextWorkRequiredForMining(&prev, &header);
    BOOST_CHECK_EQUAL(nBits, prev.nBits);        // bootstrap reuse
    BOOST_CHECK(nBits != PowLimitCompact());

    SelectParams(CBaseChainParams::UNITTEST);
}

// Post-fork but the gap is NOT strictly greater than 6h: the chooser falls
// through to the real retarget (LWMA-3 over a healthy 60-block window) and
// must not hand out min-difficulty. Exercises the genuine LWMA path with a
// linked 61-index chain.
BOOST_AUTO_TEST_CASE(miner_valve_closed_at_exact_gap)
{
    SelectParams(CBaseChainParams::MAIN);

    enum { CHAIN = 61 };
    static CBlockIndex chain[CHAIN];
    uint32_t t = 1800000000u;
    for (int i = 0; i < CHAIN; ++i) {
        chain[i].SetNull();
        chain[i].nHeight = HARDFORK_EMERGENCY_DIFF_MAIN - CHAIN + i;
        chain[i].nTime   = t; t += 120;           // healthy 2-min cadence
        chain[i].nBits   = 0x1b0404cb;
        chain[i].pprev   = (i > 0) ? &chain[i-1] : NULL;
    }
    CBlockIndex* tip = &chain[CHAIN-1];           // height = fork - 1

    CBlockHeader header;
    header.nTime = tip->nTime + EMERGENCY_DIFFICULTY_GAP;  // exactly 6h: closed

    unsigned int nBits = GetNextWorkRequiredForMining(tip, &header);
    BOOST_CHECK(nBits != PowLimitCompact());

    // One second past the strict boundary: open.
    header.nTime += 1;
    BOOST_CHECK_EQUAL(GetNextWorkRequiredForMining(tip, &header), PowLimitCompact());

    SelectParams(CBaseChainParams::UNITTEST);
}

BOOST_AUTO_TEST_SUITE_END()
