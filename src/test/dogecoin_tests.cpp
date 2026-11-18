// Copyright (c) 2015-2021 The Dogecoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <arith_uint256.h>
#include <chain.h>
#include <chainparams.h>
#include <dogecoin.h>
#include <pow.h>
#include <primitives/block.h>

#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

#include <utility>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(dogecoin_tests, TestingSetup)

// ---------------------------------------------------------------------------
// (BOB): Dogecoin's subsidy cases (random rewards, the 145k schedule) were
// removed: (BOB) never had those rules. The (BOB) subsidy is tested exactly in
// validation_tests (block_subsidy_test, subsidy_limit_test). In their place:
// the (BOB) retarget, checked against REAL mainnet headers through the same
// GetNextWorkRequired dispatcher the node validates with. KGW (13579-31596)
// is a closed regime covered block by block by the from-genesis acid test.
// The Dogecoin DigiShield cases further down still test the retained Dogecoin
// functions, which (BOB) mainnet does not use.
// ---------------------------------------------------------------------------
struct BobHdr { int h; uint32_t time; uint32_t bits; };

// (BOB) mainnet headers {height, nTime, nBits}, fetched from the live node 2026-11-17.
static const BobHdr V1_ENDPOINTS[][3] = {   // {first of window, last, target}
    {{0, 1389086657u, 0x1e0ffff0u}, {2015, 1389293899u, 0x1e0ffff0u}, {2016, 1389293911u, 0x1e03fffcu}},
    {{2016, 1389293911u, 0x1e03fffcu}, {4031, 1389303873u, 0x1e03fffcu}, {4032, 1389303875u, 0x1e00ffffu}},
};
static const BobHdr DS_CASES[][3] = {       // {h-2, h-1, target}
    {{31595, 1411019922u, 0x1c725bf6u}, {31596, 1411019925u, 0x1c6482ddu}, {31597, 1411019961u, 0x1c581d63u}},
    {{31596, 1411019925u, 0x1c6482ddu}, {31597, 1411019961u, 0x1c581d63u}, {31598, 1411019989u, 0x1c4dd5b1u}},
    {{39998, 1416680177u, 0x1c32fb1cu}, {39999, 1416680406u, 0x1c2e2341u}, {40000, 1416681603u, 0x1c2a99b9u}},
    {{68423, 1440918052u, 0x1c155236u}, {68424, 1440917908u, 0x1c1ffb51u}, {68425, 1440995855u, 0x1c17fc7cu}},
    {{68424, 1440917908u, 0x1c1ffb51u}, {68425, 1440995855u, 0x1c17fc7cu}, {68426, 1440995925u, 0x1c23fabau}},
    {{68425, 1440995855u, 0x1c17fc7cu}, {68426, 1440995925u, 0x1c23fabau}, {68427, 1440996045u, 0x1c222e30u}},
    {{499998, 1550892478u, 0x1c488b8bu}, {499999, 1550892557u, 0x1c4585bau}, {500000, 1550892787u, 0x1c42a027u}},
};
static const BobHdr LWMA_1888868[] = {   // 61-header window + target
    {1888807, 1786615512u, 0x1c46498bu}, {1888808, 1786615561u, 0x1c46498bu}, {1888809, 1786615571u, 0x1c46498bu},
    {1888810, 1786615737u, 0x1c46498bu}, {1888811, 1786615970u, 0x1c46498bu}, {1888812, 1786616071u, 0x1c46498bu},
    {1888813, 1786616098u, 0x1c46498bu}, {1888814, 1786616249u, 0x1c46498bu}, {1888815, 1786616282u, 0x1c46498bu},
    {1888816, 1786616350u, 0x1c46498bu}, {1888817, 1786616438u, 0x1c46498bu}, {1888818, 1786616720u, 0x1c46498bu},
    {1888819, 1786616748u, 0x1c46498bu}, {1888820, 1786616786u, 0x1c46498bu}, {1888821, 1786617008u, 0x1c46498bu},
    {1888822, 1786617023u, 0x1c46498bu}, {1888823, 1786617106u, 0x1c46498bu}, {1888824, 1786617125u, 0x1c46498bu},
    {1888825, 1786617236u, 0x1c46498bu}, {1888826, 1786617608u, 0x1c46498bu}, {1888827, 1786617612u, 0x1c46498bu},
    {1888828, 1786617652u, 0x1c46498bu}, {1888829, 1786617993u, 0x1c46498bu}, {1888830, 1786618053u, 0x1c46498bu},
    {1888831, 1786618167u, 0x1c46498bu}, {1888832, 1786618291u, 0x1c46498bu}, {1888833, 1786618381u, 0x1c46498bu},
    {1888834, 1786618389u, 0x1c46498bu}, {1888835, 1786618421u, 0x1c46498bu}, {1888836, 1786618502u, 0x1c46498bu},
    {1888837, 1786618574u, 0x1c46498bu}, {1888838, 1786618576u, 0x1c46498bu}, {1888839, 1786618655u, 0x1c46498bu},
    {1888840, 1786618757u, 0x1c46498bu}, {1888841, 1786619269u, 0x1c46498bu}, {1888842, 1786619569u, 0x1c46498bu},
    {1888843, 1786619742u, 0x1c46498bu}, {1888844, 1786619757u, 0x1c46498bu}, {1888845, 1786619918u, 0x1c46498bu},
    {1888846, 1786619939u, 0x1c46498bu}, {1888847, 1786620389u, 0x1c46498bu}, {1888848, 1786620485u, 0x1c46498bu},
    {1888849, 1786620560u, 0x1c46498bu}, {1888850, 1786620638u, 0x1c46498bu}, {1888851, 1786620643u, 0x1c46498bu},
    {1888852, 1786620648u, 0x1c46498bu}, {1888853, 1786620811u, 0x1c46498bu}, {1888854, 1786620853u, 0x1c46498bu},
    {1888855, 1786620858u, 0x1c46498bu}, {1888856, 1786620915u, 0x1c46498bu}, {1888857, 1786620917u, 0x1c46498bu},
    {1888858, 1786621193u, 0x1c46498bu}, {1888859, 1786621495u, 0x1c46498bu}, {1888860, 1786621569u, 0x1c46498bu},
    {1888861, 1786621671u, 0x1c46498bu}, {1888862, 1786622009u, 0x1c46498bu}, {1888863, 1786622030u, 0x1c46498bu},
    {1888864, 1786622215u, 0x1c46498bu}, {1888865, 1786622520u, 0x1c46498bu}, {1888866, 1786622572u, 0x1c46498bu},
    {1888867, 1786622818u, 0x1c46498bu}, {1888868, 1786622820u, 0x1c4d0837u},
};
static const BobHdr LWMA_1888869[] = {   // 61-header window + target
    {1888808, 1786615561u, 0x1c46498bu}, {1888809, 1786615571u, 0x1c46498bu}, {1888810, 1786615737u, 0x1c46498bu},
    {1888811, 1786615970u, 0x1c46498bu}, {1888812, 1786616071u, 0x1c46498bu}, {1888813, 1786616098u, 0x1c46498bu},
    {1888814, 1786616249u, 0x1c46498bu}, {1888815, 1786616282u, 0x1c46498bu}, {1888816, 1786616350u, 0x1c46498bu},
    {1888817, 1786616438u, 0x1c46498bu}, {1888818, 1786616720u, 0x1c46498bu}, {1888819, 1786616748u, 0x1c46498bu},
    {1888820, 1786616786u, 0x1c46498bu}, {1888821, 1786617008u, 0x1c46498bu}, {1888822, 1786617023u, 0x1c46498bu},
    {1888823, 1786617106u, 0x1c46498bu}, {1888824, 1786617125u, 0x1c46498bu}, {1888825, 1786617236u, 0x1c46498bu},
    {1888826, 1786617608u, 0x1c46498bu}, {1888827, 1786617612u, 0x1c46498bu}, {1888828, 1786617652u, 0x1c46498bu},
    {1888829, 1786617993u, 0x1c46498bu}, {1888830, 1786618053u, 0x1c46498bu}, {1888831, 1786618167u, 0x1c46498bu},
    {1888832, 1786618291u, 0x1c46498bu}, {1888833, 1786618381u, 0x1c46498bu}, {1888834, 1786618389u, 0x1c46498bu},
    {1888835, 1786618421u, 0x1c46498bu}, {1888836, 1786618502u, 0x1c46498bu}, {1888837, 1786618574u, 0x1c46498bu},
    {1888838, 1786618576u, 0x1c46498bu}, {1888839, 1786618655u, 0x1c46498bu}, {1888840, 1786618757u, 0x1c46498bu},
    {1888841, 1786619269u, 0x1c46498bu}, {1888842, 1786619569u, 0x1c46498bu}, {1888843, 1786619742u, 0x1c46498bu},
    {1888844, 1786619757u, 0x1c46498bu}, {1888845, 1786619918u, 0x1c46498bu}, {1888846, 1786619939u, 0x1c46498bu},
    {1888847, 1786620389u, 0x1c46498bu}, {1888848, 1786620485u, 0x1c46498bu}, {1888849, 1786620560u, 0x1c46498bu},
    {1888850, 1786620638u, 0x1c46498bu}, {1888851, 1786620643u, 0x1c46498bu}, {1888852, 1786620648u, 0x1c46498bu},
    {1888853, 1786620811u, 0x1c46498bu}, {1888854, 1786620853u, 0x1c46498bu}, {1888855, 1786620858u, 0x1c46498bu},
    {1888856, 1786620915u, 0x1c46498bu}, {1888857, 1786620917u, 0x1c46498bu}, {1888858, 1786621193u, 0x1c46498bu},
    {1888859, 1786621495u, 0x1c46498bu}, {1888860, 1786621569u, 0x1c46498bu}, {1888861, 1786621671u, 0x1c46498bu},
    {1888862, 1786622009u, 0x1c46498bu}, {1888863, 1786622030u, 0x1c46498bu}, {1888864, 1786622215u, 0x1c46498bu},
    {1888865, 1786622520u, 0x1c46498bu}, {1888866, 1786622572u, 0x1c46498bu}, {1888867, 1786622818u, 0x1c46498bu},
    {1888868, 1786622820u, 0x1c4d0837u}, {1888869, 1786622950u, 0x1c4ada02u},
};
static const BobHdr LWMA_1950000[] = {   // 61-header window + target
    {1949939, 1794886460u, 0x1d257993u}, {1949940, 1794886557u, 0x1d261262u}, {1949941, 1794886615u, 0x1d25dd0bu},
    {1949942, 1794886640u, 0x1d254963u}, {1949943, 1794886670u, 0x1d246526u}, {1949944, 1794886755u, 0x1d238bfau},
    {1949945, 1794886817u, 0x1d233740u}, {1949946, 1794887333u, 0x1d22aa56u}, {1949947, 1794887399u, 0x1d266b5fu},
    {1949948, 1794887419u, 0x1d25ea1fu}, {1949949, 1794887463u, 0x1d24f709u}, {1949950, 1794887466u, 0x1d243d1au},
    {1949951, 1794887721u, 0x1d231d15u}, {1949952, 1794887770u, 0x1d24683du}, {1949953, 1794887772u, 0x1d23b7f0u},
    {1949954, 1794887856u, 0x1d229271u}, {1949955, 1794887892u, 0x1d2236d3u}, {1949956, 1794888065u, 0x1d216404u},
    {1949957, 1794888435u, 0x1d21e593u}, {1949958, 1794888512u, 0x1d245296u}, {1949959, 1794888719u, 0x1d23e6a6u},
    {1949960, 1794888782u, 0x1d24bf92u}, {1949961, 1794888798u, 0x1d243000u}, {1949962, 1794889156u, 0x1d2329dbu},
    {1949963, 1794889177u, 0x1d257f2cu}, {1949964, 1794889207u, 0x1d2485f1u}, {1949965, 1794889257u, 0x1d23a25fu},
    {1949966, 1794889314u, 0x1d22f08cu}, {1949967, 1794889375u, 0x1d2250b6u}, {1949968, 1794889459u, 0x1d21ba93u},
    {1949969, 1794889916u, 0x1d215ebeu}, {1949970, 1794889926u, 0x1d24b251u}, {1949971, 1794889936u, 0x1d239b73u},
    {1949972, 1794889972u, 0x1d22849fu}, {1949973, 1794890069u, 0x1d21afaeu}, {1949974, 1794890550u, 0x1d2175c3u},
    {1949975, 1794890574u, 0x1d2507a5u}, {1949976, 1794890733u, 0x1d241561u}, {1949977, 1794890812u, 0x1d247830u},
    {1949978, 1794890843u, 0x1d241082u}, {1949979, 1794890969u, 0x1d232ffbu}, {1949980, 1794891157u, 0x1d233f54u},
    {1949981, 1794891239u, 0x1d23eb68u}, {1949982, 1794891241u, 0x1d238b87u}, {1949983, 1794891244u, 0x1d226122u},
    {1949984, 1794891312u, 0x1d21395fu}, {1949985, 1794891325u, 0x1d20b5b7u}, {1949986, 1794891358u, 0x1d1fa824u},
    {1949987, 1794891442u, 0x1d1ece73u}, {1949988, 1794891542u, 0x1d1e75d3u}, {1949989, 1794891584u, 0x1d1e4813u},
    {1949990, 1794891605u, 0x1d1d89f4u}, {1949991, 1794891672u, 0x1d1c9936u}, {1949992, 1794891697u, 0x1d1c1ec4u},
    {1949993, 1794891815u, 0x1d1b3cfeu}, {1949994, 1794892189u, 0x1d1b43cdu}, {1949995, 1794892214u, 0x1d1dba48u},
    {1949996, 1794892357u, 0x1d1cda03u}, {1949997, 1794892426u, 0x1d1d188fu}, {1949998, 1794892591u, 0x1d1ca42bu},
    {1949999, 1794892945u, 0x1d1d1637u}, {1950000, 1794893168u, 0x1d1f48fau},
};
static const BobHdr LWMA_1950100[] = {   // 61-header window + target
    {1950039, 1794898178u, 0x1d20d12eu}, {1950040, 1794898225u, 0x1d230ae2u}, {1950041, 1794898435u, 0x1d22661eu},
    {1950042, 1794898542u, 0x1d232d74u}, {1950043, 1794898730u, 0x1d230ecbu}, {1950044, 1794898801u, 0x1d23a661u},
    {1950045, 1794898913u, 0x1d23396fu}, {1950046, 1794898938u, 0x1d232879u}, {1950047, 1794899266u, 0x1d2254c5u},
    {1950048, 1794899303u, 0x1d242af8u}, {1950049, 1794899420u, 0x1d237361u}, {1950050, 1794899472u, 0x1d236fa0u},
    {1950051, 1794899513u, 0x1d22d8a0u}, {1950052, 1794899655u, 0x1d2227e1u}, {1950053, 1794899773u, 0x1d225cbeu},
    {1950054, 1794899814u, 0x1d225b23u}, {1950055, 1794899862u, 0x1d21a7dbu}, {1950056, 1794899926u, 0x1d21028eu},
    {1950057, 1794900007u, 0x1d208197u}, {1950058, 1794901048u, 0x1d202759u}, {1950059, 1794901217u, 0x1d259713u},
    {1950060, 1794901248u, 0x1d260e02u}, {1950061, 1794901332u, 0x1d2541b2u}, {1950062, 1794901349u, 0x1d24ef52u},
    {1950063, 1794901675u, 0x1d23ff3au}, {1950064, 1794901725u, 0x1d25e53cu}, {1950065, 1794901812u, 0x1d2541bau},
    {1950066, 1794901932u, 0x1d24f4fdu}, {1950067, 1794902039u, 0x1d24f61cu}, {1950068, 1794902045u, 0x1d24d899u},
    {1950069, 1794902060u, 0x1d23ca5fu}, {1950070, 1794902133u, 0x1d22cfdeu}, {1950071, 1794902195u, 0x1d225ef1u},
    {1950072, 1794902221u, 0x1d21d331u}, {1950073, 1794902236u, 0x1d20f091u}, {1950074, 1794902236u, 0x1d1ff2f1u},
    {1950075, 1794902370u, 0x1d1ed091u}, {1950076, 1794902549u, 0x1d1ef15cu}, {1950077, 1794902730u, 0x1d1f7dc0u},
    {1950078, 1794902848u, 0x1d200f82u}, {1950079, 1794902928u, 0x1d200a37u}, {1950080, 1794902980u, 0x1d1fa957u},
    {1950081, 1794903172u, 0x1d1f04bbu}, {1950082, 1794903220u, 0x1d1fb178u}, {1950083, 1794903248u, 0x1d1f03b7u},
    {1950084, 1794903578u, 0x1d1e26b9u}, {1950085, 1794903594u, 0x1d20206au}, {1950086, 1794904216u, 0x1d1f280bu},
    {1950087, 1794904313u, 0x1d23d81cu}, {1950088, 1794904325u, 0x1d23a148u}, {1950089, 1794904329u, 0x1d229eccu},
    {1950090, 1794904467u, 0x1d21884au}, {1950091, 1794904489u, 0x1d21b30au}, {1950092, 1794904631u, 0x1d20c81cu},
    {1950093, 1794904641u, 0x1d20fcdfu}, {1950094, 1794904715u, 0x1d1ff574u}, {1950095, 1794904737u, 0x1d1f889au},
    {1950096, 1794904759u, 0x1d1ea08fu}, {1950097, 1794904794u, 0x1d1db96cu}, {1950098, 1794904888u, 0x1d1cf1d5u},
    {1950099, 1794904903u, 0x1d1cb6cdu}, {1950100, 1794905003u, 0x1d1bc0ddu},
};
static const BobHdr VALVE[] = {{1899994, 1788827237u, 0x1c3b9832u}, {1899995, 1788848855u, 0x1e0fffffu}};   // emergency-difficulty block 1899995

static void SetHdr(CBlockIndex& b, const BobHdr& x)
{
    b.nHeight = x.h;
    b.nTime = x.time;
    b.nBits = x.bits;
}

// Link a contiguous window and ask the dispatcher for the next block's nBits.
static unsigned int BobNext(std::vector<CBlockIndex>& idx, uint32_t next_time, const Consensus::Params& params)
{
    for (size_t i = 0; i < idx.size(); ++i)
        idx[i].pprev = i ? &idx[i - 1] : nullptr;
    CBlockHeader next;
    next.nTime = next_time;
    return GetNextWorkRequired(&idx.back(), &next, params);
}

BOOST_AUTO_TEST_CASE(bob_retarget_v1_real_chain)
{
    // V1 reads only the window's first and last header; the rest are filler.
    const auto chainParams = CreateChainParams(*m_node.args, CBaseChainParams::MAIN);
    for (const auto& c : V1_ENDPOINTS) {
        std::vector<CBlockIndex> idx(2016);
        for (int i = 0; i < 2016; ++i) {
            idx[i].nHeight = c[0].h + i;
            idx[i].nTime = c[0].time + (uint32_t)((uint64_t)i * (c[1].time - c[0].time) / 2015);
            idx[i].nBits = c[1].bits;
        }
        SetHdr(idx[0], c[0]);
        SetHdr(idx[2015], c[1]);
        BOOST_CHECK_EQUAL(BobNext(idx, c[2].time, chainParams->GetConsensus()), c[2].bits);
    }
}

BOOST_AUTO_TEST_CASE(bob_retarget_digishield_real_chain)
{
    // V3 (T=600) from 31597, V4 (T=120) from 68425, including both first blocks.
    const auto chainParams = CreateChainParams(*m_node.args, CBaseChainParams::MAIN);
    for (const auto& c : DS_CASES) {
        std::vector<CBlockIndex> idx(2);
        SetHdr(idx[0], c[0]);
        SetHdr(idx[1], c[1]);
        BOOST_CHECK_EQUAL(BobNext(idx, c[2].time, chainParams->GetConsensus()), c[2].bits);
    }
}

BOOST_AUTO_TEST_CASE(bob_retarget_lwma3_real_chain)
{
    // The live regime: the first computed block after the bootstrap (1888868),
    // the one after it, and two recent heights. Window = 61 headers.
    const auto chainParams = CreateChainParams(*m_node.args, CBaseChainParams::MAIN);
    const std::vector<std::pair<const BobHdr*, size_t>> cases = {
        {LWMA_1888868, sizeof(LWMA_1888868) / sizeof(BobHdr)},
        {LWMA_1888869, sizeof(LWMA_1888869) / sizeof(BobHdr)},
        {LWMA_1950000, sizeof(LWMA_1950000) / sizeof(BobHdr)},
        {LWMA_1950100, sizeof(LWMA_1950100) / sizeof(BobHdr)},
    };
    for (const auto& c : cases) {
        BOOST_REQUIRE_EQUAL(c.second, 62U);
        std::vector<CBlockIndex> idx(61);
        for (size_t i = 0; i < 61; ++i) SetHdr(idx[i], c.first[i]);
        const BobHdr& target = c.first[61];
        BOOST_CHECK_EQUAL(BobNext(idx, target.time, chainParams->GetConsensus()), target.bits);
        // outside the 6h window the mining chooser is just the retarget
        CBlockHeader next;
        next.nTime = target.time;
        BOOST_CHECK_EQUAL(BobGetNextWorkRequiredForMining(&idx.back(), &next, chainParams->GetConsensus()), target.bits);
    }
}

BOOST_AUTO_TEST_CASE(bob_emergency_difficulty_valve_real_block)
{
    // Block 1899995 followed a >6h stall and claimed powLimit: the valve exemption.
    const auto chainParams = CreateChainParams(*m_node.args, CBaseChainParams::MAIN);
    const Consensus::Params& p = chainParams->GetConsensus();
    CBlockIndex prev;
    SetHdr(prev, VALVE[0]);
    CBlockHeader blk;
    blk.nTime = VALVE[1].time;
    blk.nBits = VALVE[1].bits;
    BOOST_CHECK(BobIsEmergencyDifficultyBlock(blk, &prev, p));

    CBlockHeader wrong_bits = blk;                 // must claim exactly powLimit
    wrong_bits.nBits = VALVE[0].bits;
    BOOST_CHECK(!BobIsEmergencyDifficultyBlock(wrong_bits, &prev, p));

    CBlockHeader six_hours = blk;                  // strictly MORE than 6h
    six_hours.nTime = VALVE[0].time + 21600;
    BOOST_CHECK(!BobIsEmergencyDifficultyBlock(six_hours, &prev, p));
    six_hours.nTime += 1;
    BOOST_CHECK(BobIsEmergencyDifficultyBlock(six_hours, &prev, p));

    // (BOB) mining side (0.14 v0.13.3): the template for block 1899995 claims powLimit.
    CBlockHeader tmpl;
    tmpl.nTime = VALVE[1].time;
    BOOST_CHECK_EQUAL(BobGetNextWorkRequiredForMining(&prev, &tmpl, p), UintToArith256(p.powLimit).GetCompact());
    tmpl.nTime = VALVE[0].time + 21601;            // the first second of the window
    BOOST_CHECK_EQUAL(BobGetNextWorkRequiredForMining(&prev, &tmpl, p), UintToArith256(p.powLimit).GetCompact());
}

BOOST_AUTO_TEST_CASE(get_next_work_difficulty_limit)
{
    SelectParams(CBaseChainParams::MAIN);
    const Consensus::Params& params = Params().GetConsensus();

    CBlockIndex pindexLast;
    int64_t nLastRetargetTime = 1386474927; // Block # 1

    pindexLast.nHeight = 239;
    pindexLast.nTime = 1386475638; // Block #239
    pindexLast.nBits = 0x1e0ffff0;
    BOOST_CHECK_EQUAL(CalculateDogecoinNextWorkRequired(&pindexLast, nLastRetargetTime, params), 0x1e00ffffU);
}

BOOST_AUTO_TEST_CASE(get_next_work_pre_digishield)
{
    SelectParams(CBaseChainParams::MAIN);
    const Consensus::Params& params = Params().GetConsensus();

    CBlockIndex pindexLast;
    int64_t nLastRetargetTime = 1386942008; // Block 9359

    pindexLast.nHeight = 9599;
    pindexLast.nTime = 1386954113;
    pindexLast.nBits = 0x1c1a1206;
    BOOST_CHECK_EQUAL(CalculateDogecoinNextWorkRequired(&pindexLast, nLastRetargetTime, params), 0x1c15ea59U);
}

BOOST_AUTO_TEST_CASE(get_next_work_digishield)
{
    SelectParams(CBaseChainParams::MAIN);
    const Consensus::Params& params = Params().GetConsensus();

    CBlockIndex pindexLast;
    int64_t nLastRetargetTime = 1395094427;

    // First hard-fork at 145,000, which applies to block 145,001 onwards
    pindexLast.nHeight = 145000;
    pindexLast.nTime = 1395094679;
    pindexLast.nBits = 0x1b499dfd;
    BOOST_CHECK_EQUAL(CalculateDogecoinNextWorkRequired(&pindexLast, nLastRetargetTime, params), 0x1b671062U);
}

BOOST_AUTO_TEST_CASE(get_next_work_digishield_modulated_upper)
{
    SelectParams(CBaseChainParams::MAIN);
    const Consensus::Params& params = Params().GetConsensus();

    CBlockIndex pindexLast;
    int64_t nLastRetargetTime = 1395100835;

    // Test the upper bound on modulated time using mainnet block #145,107
    pindexLast.nHeight = 145107;
    pindexLast.nTime = 1395101360;
    pindexLast.nBits = 0x1b3439cd;
    BOOST_CHECK_EQUAL(CalculateDogecoinNextWorkRequired(&pindexLast, nLastRetargetTime, params), 0x1b4e56b3U);
}

BOOST_AUTO_TEST_CASE(get_next_work_digishield_modulated_lower)
{
    SelectParams(CBaseChainParams::MAIN);
    const Consensus::Params& params = Params().GetConsensus();

    CBlockIndex pindexLast;
    int64_t nLastRetargetTime = 1395380517;

    // Test the lower bound on modulated time using mainnet block #149,423
    pindexLast.nHeight = 149423;
    pindexLast.nTime = 1395380447;
    pindexLast.nBits = 0x1b446f21;
    BOOST_CHECK_EQUAL(CalculateDogecoinNextWorkRequired(&pindexLast, nLastRetargetTime, params), 0x1b335358U);
}

BOOST_AUTO_TEST_CASE(get_next_work_digishield_rounding)
{
    SelectParams(CBaseChainParams::MAIN);
    const Consensus::Params& params = Params().GetConsensus();

    CBlockIndex pindexLast;
    int64_t nLastRetargetTime = 1395094679;

    // Test case for correct rounding of modulated time - this depends on
    // handling of integer division, and is not obvious from the code
    pindexLast.nHeight = 145001;
    pindexLast.nTime = 1395094727;
    pindexLast.nBits = 0x1b671062;
    BOOST_CHECK_EQUAL(CalculateDogecoinNextWorkRequired(&pindexLast, nLastRetargetTime, params), 0x1b6558a4U);
}

BOOST_AUTO_TEST_SUITE_END()
