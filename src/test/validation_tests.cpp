// Copyright (c) 2014-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <net.h>
#include <signet.h>
#include <validation.h>

#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(validation_tests, TestingSetup)

// (BOB): the subsidy is the fixed (BOB) GetBlockValue curve, not Bitcoin's
// "halve every nSubsidyHalvingInterval" rule. Mainnet halves 10 every 210000
// blocks until the 1.5 hard-set at 951753; testnet/regtest pay 50, 9 at 205,
// then 10 from 350 with no halvings.
BOOST_AUTO_TEST_CASE(block_subsidy_test)
{
    const auto mainParams = CreateChainParams(*m_node.args, CBaseChainParams::MAIN);
    const Consensus::Params& m = mainParams->GetConsensus();
    BOOST_CHECK_EQUAL(GetBlockSubsidy(0, m), 50 * COIN);
    BOOST_CHECK_EQUAL(GetBlockSubsidy(69791, m), 10 * COIN);
    CAmount expected = 10 * COIN;
    for (int k = 1; k <= 4; ++k) {
        expected /= 2;
        BOOST_CHECK_EQUAL(GetBlockSubsidy(k * 210000, m), expected);
        BOOST_CHECK_EQUAL(GetBlockSubsidy(k * 210000 - 1, m), expected * 2);
    }
    BOOST_CHECK_EQUAL(GetBlockSubsidy(951753, m), 150000000);
    BOOST_CHECK_EQUAL(GetBlockSubsidy(100000000, m), 150000000);   // flat forever

    const auto regParams = CreateChainParams(*m_node.args, CBaseChainParams::REGTEST);
    const Consensus::Params& r = regParams->GetConsensus();
    BOOST_CHECK_EQUAL(GetBlockSubsidy(1, r), 50 * COIN);
    BOOST_CHECK_EQUAL(GetBlockSubsidy(204, r), 50 * COIN);
    BOOST_CHECK_EQUAL(GetBlockSubsidy(205, r), 9 * COIN);
    BOOST_CHECK_EQUAL(GetBlockSubsidy(349, r), 50 * COIN);
    BOOST_CHECK_EQUAL(GetBlockSubsidy(350, r), 10 * COIN);
    BOOST_CHECK_EQUAL(GetBlockSubsidy(1000000, r), 10 * COIN);    // no halvings off mainnet

    // Guard: nSubsidyHalvingInterval must not steer the curve.
    Consensus::Params tweaked = m;
    tweaked.nSubsidyHalvingInterval = 150;
    for (int h : {150, 300, 210000, 840000})
        BOOST_CHECK_EQUAL(GetBlockSubsidy(h, tweaked), GetBlockSubsidy(h, m));
}

BOOST_AUTO_TEST_CASE(subsidy_limit_test)
{
    const auto chainParams = CreateChainParams(*m_node.args, CBaseChainParams::MAIN);
    // (BOB): every block from genesis to the 2M fork. Expected total computed
    // independently from the (BOB) 0.14 GetBlockValue (main.cpp), mainnet path.
    const Consensus::Params& p = chainParams->GetConsensus();
    CAmount nSum = 0;
    for (int nHeight = 0; nHeight <= 2000000; ++nHeight) {
        CAmount nSubsidy = GetBlockSubsidy(nHeight, p);
        BOOST_CHECK(nSubsidy <= 50 * COIN);
        nSum += nSubsidy;
        BOOST_CHECK(MoneyRange(nSum));
    }
    BOOST_CHECK_EQUAL(nSum, CAmount{837131762500000});
    // Curve landmarks: the lone 10 at 68425, back to 50 until 69791, halvings
    // of 10 every 210000, the 1.5 hard-set from 951753 on.
    BOOST_CHECK_EQUAL(GetBlockSubsidy(68424, p), 50 * COIN);
    BOOST_CHECK_EQUAL(GetBlockSubsidy(68425, p), 10 * COIN);
    BOOST_CHECK_EQUAL(GetBlockSubsidy(68426, p), 50 * COIN);
    BOOST_CHECK_EQUAL(GetBlockSubsidy(69791, p), 10 * COIN);
    BOOST_CHECK_EQUAL(GetBlockSubsidy(210000, p), 5 * COIN);
    BOOST_CHECK_EQUAL(GetBlockSubsidy(840000, p), 62500000);
    BOOST_CHECK_EQUAL(GetBlockSubsidy(951752, p), 62500000);
    BOOST_CHECK_EQUAL(GetBlockSubsidy(951753, p), 150000000);
    BOOST_CHECK_EQUAL(GetBlockSubsidy(2000000, p), 150000000);
}

BOOST_AUTO_TEST_CASE(signet_parse_tests)
{
    ArgsManager signet_argsman;
    signet_argsman.ForceSetArg("-signetchallenge", "51"); // set challenge to OP_TRUE
    const auto signet_params = CreateChainParams(signet_argsman, CBaseChainParams::SIGNET);
    CBlock block;
    BOOST_CHECK(signet_params->GetConsensus().signet_challenge == std::vector<uint8_t>{OP_TRUE});
    CScript challenge{OP_TRUE};

    // empty block is invalid
    BOOST_CHECK(!SignetTxs::Create(block, challenge));
    BOOST_CHECK(!CheckSignetBlockSolution(block, signet_params->GetConsensus()));

    // no witness commitment
    CMutableTransaction cb;
    cb.vout.emplace_back(0, CScript{});
    block.vtx.push_back(MakeTransactionRef(cb));
    block.vtx.push_back(MakeTransactionRef(cb)); // Add dummy tx to excercise merkle root code
    BOOST_CHECK(!SignetTxs::Create(block, challenge));
    BOOST_CHECK(!CheckSignetBlockSolution(block, signet_params->GetConsensus()));

    // no header is treated valid
    std::vector<uint8_t> witness_commitment_section_141{0xaa, 0x21, 0xa9, 0xed};
    for (int i = 0; i < 32; ++i) {
        witness_commitment_section_141.push_back(0xff);
    }
    cb.vout.at(0).scriptPubKey = CScript{} << OP_RETURN << witness_commitment_section_141;
    block.vtx.at(0) = MakeTransactionRef(cb);
    BOOST_CHECK(SignetTxs::Create(block, challenge));
    BOOST_CHECK(CheckSignetBlockSolution(block, signet_params->GetConsensus()));

    // no data after header, valid
    std::vector<uint8_t> witness_commitment_section_325{0xec, 0xc7, 0xda, 0xa2};
    cb.vout.at(0).scriptPubKey = CScript{} << OP_RETURN << witness_commitment_section_141 << witness_commitment_section_325;
    block.vtx.at(0) = MakeTransactionRef(cb);
    BOOST_CHECK(SignetTxs::Create(block, challenge));
    BOOST_CHECK(CheckSignetBlockSolution(block, signet_params->GetConsensus()));

    // Premature end of data, invalid
    witness_commitment_section_325.push_back(0x01);
    witness_commitment_section_325.push_back(0x51);
    cb.vout.at(0).scriptPubKey = CScript{} << OP_RETURN << witness_commitment_section_141 << witness_commitment_section_325;
    block.vtx.at(0) = MakeTransactionRef(cb);
    BOOST_CHECK(!SignetTxs::Create(block, challenge));
    BOOST_CHECK(!CheckSignetBlockSolution(block, signet_params->GetConsensus()));

    // has data, valid
    witness_commitment_section_325.push_back(0x00);
    cb.vout.at(0).scriptPubKey = CScript{} << OP_RETURN << witness_commitment_section_141 << witness_commitment_section_325;
    block.vtx.at(0) = MakeTransactionRef(cb);
    BOOST_CHECK(SignetTxs::Create(block, challenge));
    BOOST_CHECK(CheckSignetBlockSolution(block, signet_params->GetConsensus()));

    // Extraneous data, invalid
    witness_commitment_section_325.push_back(0x00);
    cb.vout.at(0).scriptPubKey = CScript{} << OP_RETURN << witness_commitment_section_141 << witness_commitment_section_325;
    block.vtx.at(0) = MakeTransactionRef(cb);
    BOOST_CHECK(!SignetTxs::Create(block, challenge));
    BOOST_CHECK(!CheckSignetBlockSolution(block, signet_params->GetConsensus()));
}

BOOST_AUTO_TEST_SUITE_END()
