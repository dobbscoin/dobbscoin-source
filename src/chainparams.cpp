// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2020 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>

#include <chainparamsseeds.h>
#include <consensus/merkle.h>
#include <hash.h> // for signet block challenge hash
#include <tinyformat.h>
#include <util/system.h>
#include <util/strencodings.h>
#include <versionbitsinfo.h>

#include <assert.h>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>

static CBlock CreateGenesisBlock(const char* pszTimestamp, const CScript& genesisOutputScript, uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = genesisOutputScript;

    CBlock genesis;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nVersion = nVersion;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    return genesis;
}

/**
 * Build the genesis block. Note that the output of its generation
 * transaction cannot be spent since it did not originally exist in the
 * database.
 *
 * CBlock(hash=000000000019d6, ver=1, hashPrevBlock=00000000000000, hashMerkleRoot=4a5e1e, nTime=1386325540, nBits=1e0ffff0, nNonce=99943, vtx=1)
 *   CTransaction(hash=4a5e1e, ver=1, vin.size=1, vout.size=1, nLockTime=0)
 *     CTxIn(COutPoint(000000, -1), coinbase 04ffff001d0104455468652054696d65732030332f4a616e2f32303039204368616e63656c6c6f72206f6e206272696e6b206f66207365636f6e64206261696c6f757420666f722062616e6b73)
 *     CTxOut(nValue=50.00000000, scriptPubKey=0x5F1DF16B2B704C8A578D0B)
 *   vMerkleTree: 4a5e1e
 */
static CBlock CreateGenesisBlock(uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    const char* pszTimestamp = "Nintondo";
    const CScript genesisOutputScript = CScript() << ParseHex("040184710fa689ad5023690c80f3a49c8f13f8d45b8c857fbcbc8bc4a8e4d3eb4b10f4d4604fa08dce601aaf0f470216fe1b51850b4acf21b179c45070ac7b03a9") << OP_CHECKSIG;
    return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTime, nNonce, nBits, nVersion, genesisReward);
}

static CBlock CreateBobGenesisBlock(uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    // (BOB)/Dobbscoin genesis, ported from dobbscoin-source src/chainparams.cpp.
    // scriptSig leads with 0xa0fb1783; 1-dobbalina output; apostrophe is U+2019 = e2 80 99.
    const char* pszTimestamp = "NY Times 05/Oct/2011 Steve Jobs, Apple" "\xe2\x80\x99" "s Visionary, Dies at 56";
    const CScript genesisOutputScript = CScript() << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f") << OP_CHECKSIG;
    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = CScript() << (int64_t)0xa0fb1783 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = genesisOutputScript;
    CBlock genesis;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nVersion = nVersion;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    return genesis;
}

/**
 * Main network
 */
class CMainParams : public CChainParams {
public:
    CMainParams() {
        strNetworkID = CBaseChainParams::MAIN;
        consensus.signet_blocks = false;
        consensus.signet_challenge.clear();
        consensus.nSubsidyHalvingInterval = 210000;
        consensus.BIP16Exception = uint256();
        // BIP34 is never enforced in Dogecoin v2 blocks, so we enforce from v3
        consensus.BIP34Height = 1; // (BOB): genesis is the only v1 block; v2+ (BIP34 height-in-coinbase) from block 1
        consensus.BIP34Hash = uint256S("0xe613b5a195556386f422875d8df68e3e154f37b4981dec641fe041211672870f"); // (BOB) block 1
        consensus.BIP65Height = 2000000; // (BOB) CLTV @ HARDFORK_CLTV_MAIN (not yet active)
        consensus.BIP66Height = 68425; // (BOB) v3/DERSIG from the 68425 fork (last v2 block = 68424)
        consensus.CSVHeight = std::numeric_limits<int>::max(); // Disabled on Dogecoin
        consensus.SegwitHeight = std::numeric_limits<int>::max(); // Disabled on Dogecoin
        consensus.MinBIP9WarningHeight = std::numeric_limits<int>::max(); // Dogecoin does not use BIP9
        consensus.powLimit = uint256S("0x00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // ~uint256(0) >> 20;
        consensus.nPowTargetTimespan = 4 * 60 * 60; // pre-digishield: 4 hours
        consensus.nPowTargetSpacing = 60; // 1 minute
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1900; // 95% of 10,080
        consensus.nMinerConfirmationWindow = 2000; // 60 * 24 * 7 = 10,080 blocks, or one week
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay

        // Deployment of Taproot (BIPs 340-342)
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].min_activation_height = 0; // No activation delay

        consensus.nMinimumChainWork = uint256S("0x000000000000000000000000000000000000000000000000007b86587882c49d"); // (BOB) chainwork at block 1945000
        consensus.defaultAssumeValid = uint256S("0xdf6e82216a8c63534d16f0a28c849f7e95907661a84a316d174a0d6b8f0fe884"); // (BOB) block 1945000 (-assumevalid=0 restores full verification)
        consensus.nAssumeValidMinTime = 60 * 60 * 24 * 7 * 2;

        consensus.fSimplifiedRewards = false;
        consensus.fShortEarlyCoinbase = false; // (BOB): flat maturity 100, no Doge sub-145000 30-block rule

        consensus.nAuxpowChainId = 0x00B0; // (BOB) AUXPOW_CHAIN_ID
        consensus.fStrictChainId = true;
        consensus.nAuxPowHeight = 2000000; // (BOB) HARDFORK_AUXPOW_MAIN

        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 32-bit integer with any alignment.
         */
        pchMessageStart[0] = 0xa0;
        pchMessageStart[1] = 0xfb;
        pchMessageStart[2] = 0x17;
        pchMessageStart[3] = 0x83;
        nDefaultPort = 19985;
        nPruneAfterHeight = 100000;
        m_assumed_blockchain_size = 350;
        m_assumed_chain_state_size = 6;

        genesis = CreateBobGenesisBlock(1389086657, 2084524493, 0x1e0ffff0, 1, 1); // (BOB) genesis acid-test
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x6d2d7d525900712451b9697d0b5b2304ebae6efb349540da445bf575c0159969"));
        assert(genesis.hashMerkleRoot == uint256S("0x21e66ff8b6f14eb3fa0edf35dc93440a751fdddb421965c88029ad0048d98ca1"));

        // Note that of those which support the service bits prefix, most only support a subset of
        // possible options.
        // This is fine at runtime as we'll fall back to using them as an addrfetch if they don't support the
        // service bits we want, but we should get them updated to support all service bits wanted by any
        // release ASAP to avoid it where possible.
        // (BOB) 0.14.1's DNS seeds, unchanged.
        vSeeds.emplace_back("seed.dobbscoin.info");
        vSeeds.emplace_back("seed1.dobbscoin.info");
        vSeeds.emplace_back("seed2.dobbscoin.info");
        vSeeds.emplace_back("seed3.dobbscoin.info");
        vSeeds.emplace_back("seed4.dobbscoin.info");
        vSeeds.emplace_back("seed5.dobbscoin.info");
        vSeeds.emplace_back("seed6.dobbscoin.info");
        vSeeds.emplace_back("seed7.dobbscoin.info");
        vSeeds.emplace_back("seed8.dobbscoin.info");
        vSeeds.emplace_back("seed9.dobbscoin.info");
        vSeeds.emplace_back("seed10.dobbscoin.info");
        // A second domain, as in 0.14: dobbscoin.info renews 2027-01-18, six days before
        // the block 2,000,000 fork; these keep a new node able to find the network by name.
        vSeeds.emplace_back("jhvh1.23skidoo.info"); // JHVH-1
        vSeeds.emplace_back("dobbs.23skidoo.info"); // "Bob" Dobbs
        vSeeds.emplace_back("eris.23skidoo.info"); // Eris
        vSeeds.emplace_back("connie.23skidoo.info"); // Connie Dobbs
        vSeeds.emplace_back("nheeghee.23skidoo.info"); // the Nhee Ghee

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,0);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,5);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,128);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x88, 0xB2, 0x1E};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x88, 0xAD, 0xE4};

        bech32_hrp = "bob";

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_main, pnSeed6_main + ARRAYLEN(pnSeed6_main));

        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        m_is_test_chain = false;
        m_is_mockable_chain = false;

        checkpointData = {
            {
                // (BOB) 0.14.1's checkpoints, unchanged (each checked against the live chain 2026-11-17).
                {      0, uint256S("0x6d2d7d525900712451b9697d0b5b2304ebae6efb349540da445bf575c0159969")},
                {   2016, uint256S("0x94a9ab39b9e1e7dafdda1a5823c11556750157aa16e93ae2a59de426e185de58")},
                {   4032, uint256S("0xa29010ca0ec82b0d2d937f5d82d2e30918467b1f4b3bb51026d13782b7aac73a")},
                {   8064, uint256S("0x76ef2061b587842f424aae28fcab25fb8434612b58125258292799aaaed0f833")},
                {  12096, uint256S("0x5abe39584788a018296d97a6f00ca80c81603ebbe8ad84af5778e7d6fcdbed70")},
                {  13422, uint256S("0xc70a40fe1c53e5525636644e625d4c38c8493b180a0ca81c9d7c9801748e6b52")},
                {  14496, uint256S("0xd50a2656a6ac593ebb034c1d5ca5eac93220455727f677e7506629f61d566239")},
                {  15809, uint256S("0x31cdc96eb5510e69bf2635b76b0266c0065839685bb30d9b94224d9a5c972b78")},
                {  15882, uint256S("0xbfbbf161974b59797b5698b086fd8a5efab4786a3cee389d130da20e03d65eaf")},
                {  30800, uint256S("0x3a767fc61883f7d5adfd2b07c810ec4b065812dd40aafbda1944f8f792e2c00a")},
                {  66138, uint256S("0x8f4074d04838fb1d0914d46ee35de8b1e069f70df97e207617210469d1ebcf7e")},
                {  68900, uint256S("0xe0bef613116487194c2c823110d9b4d630ccd33d186b9dc12db03c03ae21c958")},
                { 318371, uint256S("0xabfd65da327b3b1eef32bceed1219b415e1f6d075c242dbc05d8b9d34196afb9")},
                {1848000, uint256S("0xf4e2e967ae88e49a915ed9c3c21faa2bca15c0bb6258ac16c769ccb3962f3615")},
            }
        };

        chainTxData = ChainTxData{
            // (BOB) data from RPC: getchaintxstats 4096 6d2de99b5142eec1146d0bbe1c90a45c6d0f4762f4b1254dd99cfb59519d812a
            // (block 1950461, on the rebased shadow node; it was Dogecoin data, which made the
            // GUI show about 1% progress at a 7-week-old tip)
            /* nTime    */ 1794949414,
            /* nTxCount */ 2183167,
            /* dTxRate  */ 0.009953928225024248,
        };
    }
};

/**
 * Testnet (v3)
 */
class CTestNetParams : public CChainParams {
public:
    CTestNetParams() {
        // (BOB) 0.14's testnet: CTestNetParams there derives from the mainnet params and
        // changes the genesis time/nonce, magic, ports, prefixes and the test-chain rules
        // below; the fork heights are 0.14 pow.h's *_TESTNET constants.
        strNetworkID = CBaseChainParams::TESTNET;
        consensus.signet_blocks = false;
        consensus.signet_challenge.clear();
        consensus.nSubsidyHalvingInterval = 210000; // unused: the (BOB) testnet subsidy never halves
        consensus.BIP16Exception = uint256();
        // 0.14 used version majorities (51/75 of 100); its miners stamp v3 from block 1,
        // so v2+/v3+ from block 1 rejects nothing a 0.14 testnet miner produces.
        consensus.BIP34Height = 1;
        consensus.BIP34Hash = uint256();
        consensus.BIP65Height = 150; // HARDFORK_CLTV_TESTNET
        consensus.BIP66Height = 1;
        consensus.CSVHeight = std::numeric_limits<int>::max();
        consensus.SegwitHeight = std::numeric_limits<int>::max();
        consensus.MinBIP9WarningHeight = std::numeric_limits<int>::max();
        consensus.powLimit = uint256S("0x00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // as mainnet
        consensus.nPowTargetTimespan = 4 * 60 * 60;
        consensus.nPowTargetSpacing = 60;
        consensus.fPowAllowMinDifficultyBlocks = true; // 0.14 fAllowMinDifficultyBlocks
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 750;
        consensus.nMinerConfirmationWindow = 1000;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].min_activation_height = 0;
        consensus.nMinimumChainWork = uint256S("0x00"); // no testnet chain is running yet
        consensus.defaultAssumeValid = uint256S("0x00"); // testnet is cheap to verify in full (0.14)
        consensus.nAssumeValidMinTime = 0;
        consensus.fSimplifiedRewards = false;
        consensus.fShortEarlyCoinbase = false; // flat maturity 100, as mainnet
        consensus.nAuxpowChainId = 0x00B0;  // AUXPOW_CHAIN_ID, as mainnet
        consensus.fStrictChainId = true;
        consensus.nAuxPowHeight = 10;       // HARDFORK_AUXPOW_TESTNET

        pchMessageStart[0] = 0xfa;
        pchMessageStart[1] = 0xda;
        pchMessageStart[2] = 0x0b;
        pchMessageStart[3] = 0xbf;
        nDefaultPort = 39985;
        nPruneAfterHeight = 1000;
        m_assumed_blockchain_size = 1;
        m_assumed_chain_state_size = 1;

        // The mainnet genesis coinbase with 0.14's testnet nTime and nonce.
        genesis = CreateBobGenesisBlock(1393990003, 2181768, 0x1e0ffff0, 1, 1);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x0d44ae9be7f13a64253c9b61dbbddc37304e1c359fd593565caf356e4fd597c7"));
        assert(genesis.hashMerkleRoot == uint256S("0x21e66ff8b6f14eb3fa0edf35dc93440a751fdddb421965c88029ad0048d98ca1"));

        vFixedSeeds.clear();
        vSeeds.clear();
        vSeeds.emplace_back("testnet-seed.dobbscoin.info");

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32_hrp = "tbob";

        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        m_is_test_chain = true;
        m_is_mockable_chain = false;

        checkpointData = {
            {
                {0, uint256S("0x0d44ae9be7f13a64253c9b61dbbddc37304e1c359fd593565caf356e4fd597c7")},
            }
        };

        chainTxData = ChainTxData{0, 0, 0};
    }
};

/**
 * Signet
 */
class SigNetParams : public CChainParams {
public:
    explicit SigNetParams(const ArgsManager& args) {
        std::vector<uint8_t> bin;
        vSeeds.clear();

        if (!args.IsArgSet("-signetchallenge")) {
            // Dogecoin: Use OP_FALSE as challenge, as we have no default Signet
            bin = ParseHex("50");
        } else {
            const auto signet_challenge = args.GetArgs("-signetchallenge");
            if (signet_challenge.size() != 1) {
                throw std::runtime_error(strprintf("%s: -signetchallenge cannot be multiple values.", __func__));
            }
            bin = ParseHex(signet_challenge[0]);
            LogPrintf("Signet with challenge %s\n", signet_challenge[0]);
        }

        consensus.nMinimumChainWork = uint256{};
        consensus.defaultAssumeValid = uint256{};
        m_assumed_blockchain_size = 0;
        m_assumed_chain_state_size = 0;
        chainTxData = ChainTxData{
            0,
            0,
            0,
        };

        if (args.IsArgSet("-signetseednode")) {
            vSeeds = args.GetArgs("-signetseednode");
        }

        strNetworkID = CBaseChainParams::SIGNET;
        consensus.signet_blocks = true;
        consensus.signet_challenge.assign(bin.begin(), bin.end());
        consensus.nSubsidyHalvingInterval = 100000;
        consensus.BIP16Exception = uint256{};
        consensus.BIP34Height = 1;
        consensus.BIP34Hash = uint256{};
        consensus.BIP65Height = 1;
        consensus.BIP66Height = 1;
        consensus.CSVHeight = 1;
        consensus.SegwitHeight = 1;
        consensus.nPowTargetTimespan = 4 * 60 * 60; // pre-digishield: 4 hours
        consensus.nPowTargetSpacing = 60; // 1 minute
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = false;
        consensus.nAssumeValidMinTime = 60 * 60 * 24 * 7 * 2;
        consensus.nRuleChangeActivationThreshold = 1900; // 95% of 2000
        consensus.nMinerConfirmationWindow = 2000; // nPowTargetTimespan / nPowTargetSpacing
        consensus.MinBIP9WarningHeight = 0;
        consensus.powLimit = uint256S("0x0000f77ae0000000000000000000000000000000000000000000000000000000");
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay

        // Activation of Taproot (BIPs 340-342)
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].min_activation_height = 0; // No activation delay

        consensus.fSimplifiedRewards = true;
        consensus.fShortEarlyCoinbase = true;

        consensus.nAuxpowChainId = 0x0062; // 98 - Josh Wise!
        consensus.fStrictChainId = true;
        consensus.nAuxPowHeight = 20;

        // message start is defined as the first 4 bytes of the sha256d of the block script
        CHashWriter h(SER_DISK, 0);
        h << consensus.signet_challenge;
        uint256 hash = h.GetHash();
        memcpy(pchMessageStart, hash.begin(), 4);

        nDefaultPort = 55556;
        nPruneAfterHeight = 1000;

        genesis = CreateGenesisBlock(1622364566, 81621, 0x1f00f77a, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0xf21a2c00a3b58b3f1b245de5e30955c00784040a3e7042edb0d2eedd1fd085a5"));
        assert(genesis.hashMerkleRoot == uint256S("0xb60e6a649c2c43248d6d8da2fb19daa337511fabfe3875f382adb686474fc021"));

        vFixedSeeds.clear();

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,113);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,241);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32_hrp = "tdge";

        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        m_is_test_chain = true;
        m_is_mockable_chain = false;
    }
};

/**
 * Regression test
 */
class CRegTestParams : public CChainParams {
public:
    explicit CRegTestParams(const ArgsManager& args) {
        strNetworkID =  CBaseChainParams::REGTEST;
        consensus.signet_blocks = false;
        consensus.signet_challenge.clear();
        consensus.nSubsidyHalvingInterval = 150;
        consensus.BIP16Exception = uint256();
        // Dogecoin: Raise this a long way so it's below 3 * COINBASE_MATURITY still
        consensus.BIP34Height = 1000; // BIP34 activated on regtest (Used in functional tests)
        consensus.BIP34Hash = uint256();
        consensus.BIP65Height = 150; // (BOB) 0.14 HARDFORK_CLTV_TESTNET; -bip65height overrides
        consensus.BIP66Height = 1251; // BIP66 activated on regtest (Used in functional tests)
        // Dogecoin: Note this needs to be a multiple of nMinerConfirmationWindow
        // (BOB): off as on mainnet and in 0.14 (no SegWit/CSV); -segwitheight still overrides.
        consensus.CSVHeight = std::numeric_limits<int>::max();
        consensus.SegwitHeight = std::numeric_limits<int>::max();
        consensus.MinBIP9WarningHeight = 0;
        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan =  4 * 60 * 60; // pre-digishield: 4 hours
        consensus.nPowTargetSpacing = 60; // regtest: 60 second blocks
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = true;
        consensus.nRuleChangeActivationThreshold = 108; // 75% for testchains
        consensus.nMinerConfirmationWindow = 144; // Faster than normal for regtest (144 instead of 10080)

        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay

        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].min_activation_height = 0; // No activation delay

        consensus.nMinimumChainWork = uint256{};
        consensus.defaultAssumeValid = uint256{};
        consensus.nAssumeValidMinTime = 60 * 60 * 24 * 7 * 2 / 10; // One tenth of main/test networks

        consensus.fSimplifiedRewards = true;
        consensus.fShortEarlyCoinbase = false;

        consensus.nAuxpowChainId = 0x00B0; // (BOB) AUXPOW_CHAIN_ID
        consensus.fStrictChainId = true;
        consensus.nAuxPowHeight = 10; // (BOB) 0.14 HARDFORK_AUXPOW_TESTNET; -auxpowheight overrides

        pchMessageStart[0] = 0xfa;
        pchMessageStart[1] = 0xbf;
        pchMessageStart[2] = 0xb5;
        pchMessageStart[3] = 0xda;
        nDefaultPort = 18444;
        nPruneAfterHeight = 1000;
        m_assumed_blockchain_size = 0;
        m_assumed_chain_state_size = 0;

        UpdateActivationParametersFromArgs(args);

        // (BOB) 0.14 regtest genesis: mainnet coinbase, nTime 1296688603, nonce 2.
        genesis = CreateBobGenesisBlock(1296688603, 2, 0x207fffff, 1, 1);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x63c57ac1f1acc24a6ad8f9e938237f42370ad80a3fba52b818c35140db98a07f"));
        assert(genesis.hashMerkleRoot == uint256S("0x21e66ff8b6f14eb3fa0edf35dc93440a751fdddb421965c88029ad0048d98ca1"));

        vFixedSeeds.clear(); //!< Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();      //!< Regtest mode doesn't have any DNS seeds.

        fDefaultConsistencyChecks = true;
        fRequireStandard = true;
        m_is_test_chain = true;
        m_is_mockable_chain = true;

        checkpointData = {
            {
                {0, uint256S("63c57ac1f1acc24a6ad8f9e938237f42370ad80a3fba52b818c35140db98a07f")},
            }
        };

        chainTxData = ChainTxData{
            0,
            0,
            0
        };

        // (BOB) 0.14 regtest prefixes.
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32_hrp = "dcrt";
    }

    /**
     * Allows modifying the Version Bits regtest parameters.
     */
    void UpdateVersionBitsParameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout, int min_activation_height)
    {
        consensus.vDeployments[d].nStartTime = nStartTime;
        consensus.vDeployments[d].nTimeout = nTimeout;
        consensus.vDeployments[d].min_activation_height = min_activation_height;
    }
    void UpdateActivationParametersFromArgs(const ArgsManager& args);
};

void CRegTestParams::UpdateActivationParametersFromArgs(const ArgsManager& args)
{
    if (args.IsArgSet("-bip65height")) {
        int64_t height = args.GetArg("-bip65height", consensus.BIP65Height);
        if (height < 0 || height >= std::numeric_limits<int>::max())
            throw std::runtime_error(strprintf("BIP65 activation height %ld is out of valid range.", height));
        consensus.BIP65Height = static_cast<int>(height);
    }
    if (args.IsArgSet("-auxpowheight")) {
        int64_t height = args.GetArg("-auxpowheight", consensus.nAuxPowHeight);
        if (height < 0 || height >= std::numeric_limits<int>::max())
            throw std::runtime_error(strprintf("AuxPoW activation height %ld is out of valid range.", height));
        consensus.nAuxPowHeight = static_cast<int>(height);
    }
    if (args.IsArgSet("-segwitheight")) {
        int64_t height = args.GetArg("-segwitheight", consensus.SegwitHeight);
        if (height < -1 || height >= std::numeric_limits<int>::max()) {
            throw std::runtime_error(strprintf("Activation height %ld for segwit is out of valid range. Use -1 to disable segwit.", height));
        } else if (height == -1) {
            LogPrintf("Segwit disabled for testing\n");
            height = std::numeric_limits<int>::max();
        }
        consensus.SegwitHeight = static_cast<int>(height);
    }

    if (!args.IsArgSet("-vbparams")) return;

    for (const std::string& strDeployment : args.GetArgs("-vbparams")) {
        std::vector<std::string> vDeploymentParams;
        boost::split(vDeploymentParams, strDeployment, boost::is_any_of(":"));
        if (vDeploymentParams.size() < 3 || 4 < vDeploymentParams.size()) {
            throw std::runtime_error("Version bits parameters malformed, expecting deployment:start:end[:min_activation_height]");
        }
        int64_t nStartTime, nTimeout;
        int min_activation_height = 0;
        if (!ParseInt64(vDeploymentParams[1], &nStartTime)) {
            throw std::runtime_error(strprintf("Invalid nStartTime (%s)", vDeploymentParams[1]));
        }
        if (!ParseInt64(vDeploymentParams[2], &nTimeout)) {
            throw std::runtime_error(strprintf("Invalid nTimeout (%s)", vDeploymentParams[2]));
        }
        if (vDeploymentParams.size() >= 4 && !ParseInt32(vDeploymentParams[3], &min_activation_height)) {
            throw std::runtime_error(strprintf("Invalid min_activation_height (%s)", vDeploymentParams[3]));
        }
        bool found = false;
        for (int j=0; j < (int)Consensus::MAX_VERSION_BITS_DEPLOYMENTS; ++j) {
            if (vDeploymentParams[0] == VersionBitsDeploymentInfo[j].name) {
                UpdateVersionBitsParameters(Consensus::DeploymentPos(j), nStartTime, nTimeout, min_activation_height);
                found = true;
                LogPrintf("Setting version bits activation parameters for %s to start=%ld, timeout=%ld, min_activation_height=%d\n", vDeploymentParams[0], nStartTime, nTimeout, min_activation_height);
                break;
            }
        }
        if (!found) {
            throw std::runtime_error(strprintf("Invalid deployment (%s)", vDeploymentParams[0]));
        }
    }
}

static std::unique_ptr<const CChainParams> globalChainParams;

const CChainParams &Params() {
    assert(globalChainParams);
    return *globalChainParams;
}

std::unique_ptr<const CChainParams> CreateChainParams(const ArgsManager& args, const std::string& chain)
{
    if (chain == CBaseChainParams::MAIN) {
        return std::unique_ptr<CChainParams>(new CMainParams());
    } else if (chain == CBaseChainParams::TESTNET) {
        return std::unique_ptr<CChainParams>(new CTestNetParams());
    } else if (chain == CBaseChainParams::SIGNET) {
        return std::unique_ptr<CChainParams>(new SigNetParams(args));
    } else if (chain == CBaseChainParams::REGTEST) {
        return std::unique_ptr<CChainParams>(new CRegTestParams(args));
    }
    throw std::runtime_error(strprintf("%s: Unknown chain %s.", __func__, chain));
}

void SelectParams(const std::string& network)
{
    SelectBaseParams(network);
    globalChainParams = CreateChainParams(gArgs, network);
}
