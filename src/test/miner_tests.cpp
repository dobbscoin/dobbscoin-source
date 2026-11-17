// Copyright (c) 2011-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <coins.h>
#include <consensus/consensus.h>
#include <consensus/merkle.h>
#include <consensus/tx_verify.h>
#include <miner.h>
#include <policy/policy.h>
#include <script/standard.h>
#include <txmempool.h>
#include <uint256.h>
#include <util/strencodings.h>
#include <util/system.h>
#include <util/time.h>
#include <validation.h>

#include <test/util/setup_common.h>

#include <memory>

#include <boost/test/unit_test.hpp>

namespace miner_tests {
struct MinerTestingSetup : public TestingSetup {
    void TestPackageSelection(const CChainParams& chainparams, const CScript& scriptPubKey, const std::vector<CTransactionRef>& txFirst) EXCLUSIVE_LOCKS_REQUIRED(::cs_main, m_node.mempool->cs);
    bool TestSequenceLocks(const CTransaction& tx, int flags) EXCLUSIVE_LOCKS_REQUIRED(::cs_main, m_node.mempool->cs)
    {
        return CheckSequenceLocks(*m_node.mempool, tx, flags);
    }
    BlockAssembler AssemblerForTest(const CChainParams& params);
};
} // namespace miner_tests

BOOST_FIXTURE_TEST_SUITE(miner_tests, MinerTestingSetup)

static CFeeRate blockMinFeeRate = CFeeRate(DEFAULT_BLOCK_MIN_TX_FEE);

BlockAssembler MinerTestingSetup::AssemblerForTest(const CChainParams& params)
{
    BlockAssembler::Options options;

    options.nBlockMaxWeight = MAX_BLOCK_WEIGHT;
    options.blockMinFeeRate = blockMinFeeRate;
    return BlockAssembler(*m_node.mempool, params, options);
}

constexpr static struct {
    unsigned char extranonce;
    unsigned int nonce;
} blockinfo[] = {
    // (BOB): lowest valid nonces for (BOB) mainnet (genesis 6d2d7d52..., nBits 0x1e0ffff0),
    // with the BIP34 height-first coinbase below. Ground 2026-11-15.
    {4, 0x000320e6}, {2, 0x0001509c}, {1, 0x000162af}, {2, 0x000875bb},
    {1, 0x003fbc65}, {2, 0x004539e8}, {1, 0x000dc047}, {2, 0x0008126b},
    {2, 0x0015b936}, {1, 0x0011b3f3}, {1, 0x000d2b71}, {2, 0x0011d25a},
    {2, 0x00055d69}, {1, 0x001ae65d}, {1, 0x00015cdd}, {2, 0x0033c25b},
    {2, 0x00079dde}, {1, 0x0014988e}, {2, 0x000337a8}, {2, 0x0036ecd9},
    {1, 0x0004e146}, {2, 0x00260ad2}, {1, 0x00162805}, {3, 0x00082337},
    {3, 0x000c956b}, {2, 0x000bcaa3}, {2, 0x002256b7}, {1, 0x00018ae8},
    {2, 0x0021b296}, {1, 0x00006bb4}, {2, 0x00060d14}, {2, 0x0000bb0c},
    {2, 0x000a36ba}, {2, 0x00179185}, {2, 0x0020c20c}, {2, 0x000967d2},
    {1, 0x001e9b49}, {2, 0x001b3a62}, {2, 0x000df54c}, {1, 0x001149d5},
    {2, 0x00007468}, {1, 0x000cf429}, {2, 0x0013219c}, {1, 0x0006b33b},
    {1, 0x000fe2dd}, {3, 0x001aa061}, {2, 0x000912fd}, {5, 0x000354f4},
    {1, 0x00008ad5}, {5, 0x000b2084}, {1, 0x0001dcc7}, {1, 0x001392af},
    {1, 0x00062b69}, {2, 0x00279f95}, {1, 0x0004caf1}, {1, 0x00031557},
    {1, 0x00329928}, {1, 0x000b62d1}, {5, 0x0009e144}, {5, 0x000562d2},
    {1, 0x0022420e}, {1, 0x0008e146}, {6, 0x000cd933}, {2, 0x001a3dba},
    {2, 0x0009dc9e}, {1, 0x00038be3}, {1, 0x0003cd79}, {1, 0x00073089},
    {2, 0x00073e3e}, {2, 0x000ec8f3}, {1, 0x0007ddf2}, {1, 0x002d5a7d},
    {1, 0x00096685}, {5, 0x0002e73f}, {5, 0x000c7c00}, {1, 0x0029ac08},
    {1, 0x003f9776}, {2, 0x000dc4d6}, {2, 0x00030f4c}, {1, 0x00026e94},
    {2, 0x0017fd42}, {1, 0x000aba45}, {2, 0x0004165f}, {2, 0x0006c4dd},
    {1, 0x0021b3e9}, {1, 0x0000b982}, {1, 0x0010d592}, {5, 0x00022d45},
    {1, 0x0002f1d4}, {1, 0x0027ae69}, {1, 0x0018de0c}, {1, 0x0000d428},
    {1, 0x000a4e4e}, {1, 0x000c4375}, {1, 0x0005d3ab}, {2, 0x00126742},
    {0, 0x000b7f55}, {1, 0x000a5afe}, {2, 0x0025999b}, {2, 0x00026e77},
    {1, 0x002e7878}, {1, 0x00069b41}, {1, 0x0008d0e8}, {1, 0x00051fe5},
    {1, 0x00001c1e}, {1, 0x001bdabd}, {1, 0x00085fb5}, {2, 0x00048c27},
    {0, 0x001279d5}, {1, 0x00021023},
};

static CBlockIndex CreateBlockIndex(int nHeight) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    CBlockIndex index;
    index.nHeight = nHeight;
    index.pprev = ::ChainActive().Tip();
    return index;
}

// Test suite for ancestor feerate transaction selection.
// Implemented as an additional function, rather than a separate test case,
// to allow reusing the blockchain created in CreateNewBlock_validity.
void MinerTestingSetup::TestPackageSelection(const CChainParams& chainparams, const CScript& scriptPubKey, const std::vector<CTransactionRef>& txFirst)
{
    // Test the ancestor feerate transaction selection.
    TestMemPoolEntryHelper entry;

    // Test that a medium fee transaction will be selected after a higher fee
    // rate package with a low fee rate parent.
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].scriptSig = CScript() << OP_1;
    tx.vin[0].prevout.hash = txFirst[0]->GetHash();
    tx.vin[0].prevout.n = 0;
    tx.vout.resize(1);
    tx.vout[0].nValue = 5000000000LL - 1000;
    // This tx has a low fee: 1000 satoshis
    uint256 hashParentTx = tx.GetHash(); // save this txid for later use
    m_node.mempool->addUnchecked(entry.Fee(1000).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));

    // This tx has a medium fee: 10000 satoshis
    tx.vin[0].prevout.hash = txFirst[1]->GetHash();
    tx.vout[0].nValue = 5000000000LL - 10000;
    uint256 hashMediumFeeTx = tx.GetHash();
    m_node.mempool->addUnchecked(entry.Fee(10000).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));

    // This tx has a high fee, but depends on the first transaction
    tx.vin[0].prevout.hash = hashParentTx;
    tx.vout[0].nValue = 5000000000LL - 1000 - 50000; // 50k satoshi fee
    uint256 hashHighFeeTx = tx.GetHash();
    m_node.mempool->addUnchecked(entry.Fee(50000).Time(GetTime()).SpendsCoinbase(false).FromTx(tx));

    std::unique_ptr<CBlockTemplate> pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey);
    BOOST_CHECK(pblocktemplate->block.vtx[1]->GetHash() == hashParentTx);
    BOOST_CHECK(pblocktemplate->block.vtx[2]->GetHash() == hashHighFeeTx);
    BOOST_CHECK(pblocktemplate->block.vtx[3]->GetHash() == hashMediumFeeTx);

    // Test that a package below the block min tx fee doesn't get included
    tx.vin[0].prevout.hash = hashHighFeeTx;
    tx.vout[0].nValue = 5000000000LL - 1000 - 50000; // 0 fee
    uint256 hashFreeTx = tx.GetHash();
    m_node.mempool->addUnchecked(entry.Fee(0).FromTx(tx));
    size_t freeTxSize = ::GetSerializeSize(tx, PROTOCOL_VERSION);

    // Calculate a fee on child transaction that will put the package just
    // below the block min tx fee (assuming 1 child tx of the same size).
    CAmount feeToUse = blockMinFeeRate.GetFee(2*freeTxSize) - 1;

    tx.vin[0].prevout.hash = hashFreeTx;
    tx.vout[0].nValue = 5000000000LL - 1000 - 50000 - feeToUse;
    uint256 hashLowFeeTx = tx.GetHash();
    m_node.mempool->addUnchecked(entry.Fee(feeToUse).FromTx(tx));
    pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey);
    // Verify that the free tx and the low fee tx didn't get selected
    for (size_t i=0; i<pblocktemplate->block.vtx.size(); ++i) {
        BOOST_CHECK(pblocktemplate->block.vtx[i]->GetHash() != hashFreeTx);
        BOOST_CHECK(pblocktemplate->block.vtx[i]->GetHash() != hashLowFeeTx);
    }

    // Test that packages above the min relay fee do get included, even if one
    // of the transactions is below the min relay fee
    // Remove the low fee transaction and replace with a higher fee transaction
    m_node.mempool->removeRecursive(CTransaction(tx), MemPoolRemovalReason::REPLACED);
    tx.vout[0].nValue -= 2; // Now we should be just over the min relay fee
    hashLowFeeTx = tx.GetHash();
    m_node.mempool->addUnchecked(entry.Fee(feeToUse+2).FromTx(tx));
    pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey);
    BOOST_CHECK(pblocktemplate->block.vtx[4]->GetHash() == hashFreeTx);
    BOOST_CHECK(pblocktemplate->block.vtx[5]->GetHash() == hashLowFeeTx);

    // Test that transaction selection properly updates ancestor fee
    // calculations as ancestor transactions get included in a block.
    // Add a 0-fee transaction that has 2 outputs.
    tx.vin[0].prevout.hash = txFirst[2]->GetHash();
    tx.vout.resize(2);
    tx.vout[0].nValue = 5000000000LL - 100000000;
    tx.vout[1].nValue = 100000000; // 1BTC output
    uint256 hashFreeTx2 = tx.GetHash();
    m_node.mempool->addUnchecked(entry.Fee(0).SpendsCoinbase(true).FromTx(tx));

    // This tx can't be mined by itself
    tx.vin[0].prevout.hash = hashFreeTx2;
    tx.vout.resize(1);
    feeToUse = blockMinFeeRate.GetFee(freeTxSize);
    tx.vout[0].nValue = 5000000000LL - 100000000 - feeToUse;
    uint256 hashLowFeeTx2 = tx.GetHash();
    m_node.mempool->addUnchecked(entry.Fee(feeToUse).SpendsCoinbase(false).FromTx(tx));
    pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey);

    // Verify that this tx isn't selected.
    for (size_t i=0; i<pblocktemplate->block.vtx.size(); ++i) {
        BOOST_CHECK(pblocktemplate->block.vtx[i]->GetHash() != hashFreeTx2);
        BOOST_CHECK(pblocktemplate->block.vtx[i]->GetHash() != hashLowFeeTx2);
    }

    // This tx will be mineable, and should cause hashLowFeeTx2 to be selected
    // as well.
    tx.vin[0].prevout.n = 1;
    tx.vout[0].nValue = 100000000 - 10000; // 10k satoshi fee
    m_node.mempool->addUnchecked(entry.Fee(10000).FromTx(tx));
    pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey);
    BOOST_CHECK(pblocktemplate->block.vtx[8]->GetHash() == hashLowFeeTx2);
}

// NOTE: These tests rely on CreateNewBlock doing its own self-validation!
BOOST_AUTO_TEST_CASE(CreateNewBlock_validity)
{
    // Note that by default, these tests run with size accounting enabled.
    const auto chainParams = CreateChainParams(*m_node.args, CBaseChainParams::MAIN);
    const CChainParams& chainparams = *chainParams;
    // changed this to dogecoin genesis pubkey script
    CScript scriptPubKey = CScript() << ParseHex("040184710fa689ad5023690c80f3a49c8f13f8d45b8c857fbcbc8bc4a8e4d3eb4b10f4d4604fa08dce601aaf0f470216fe1b51850b4acf21b179c45070ac7b03a9") << OP_CHECKSIG;
    std::unique_ptr<CBlockTemplate> pblocktemplate;
    CMutableTransaction tx;
    CScript script;
    uint256 hash;
    TestMemPoolEntryHelper entry;
    entry.nFee = 11;
    entry.nHeight = 11;

    fCheckpointsEnabled = false;

    // Simple block creation, nothing special yet:
    BOOST_CHECK(pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey));

    // We can't make transactions until we have inputs
    // Therefore, load 110 blocks :)
    static_assert(sizeof(blockinfo) / sizeof(*blockinfo) == 110, "Should have 110 blocks to import");
    int baseheight = 0;
    std::vector<CTransactionRef> txFirst;
    for (unsigned int i = 0; i < sizeof(blockinfo)/sizeof(*blockinfo); ++i)
    {
        CBlock *pblock = &pblocktemplate->block; // pointer for convenience
        {
            LOCK(cs_main);
            pblock->nVersion = 1;
            // Replaced ::ChainActive().Tip()->GetMedianTimePast()+1 with an actual 60 second block
            // interval because median([1,2,2,3,3,3,4,4,4,4]) will eventually be problematic re:
            // block timing. Tests should be more stable than that.
            pblock->nTime = ::ChainActive().Tip()->GetBlockTime() + 60;
            CMutableTransaction txCoinbase(*pblock->vtx[0]);
            txCoinbase.nVersion = 1;
            // (BOB): BIP34 is active from height 1, so the coinbase must lead with the height.
            txCoinbase.vin[0].scriptSig = CScript() << (::ChainActive().Height() + 1) << CScriptNum(blockinfo[i].extranonce);
            txCoinbase.vout.resize(1); // Ignore the (optional) segwit commitment added by CreateNewBlock (as the hardcoded nonces don't account for this)
            txCoinbase.vout[0].scriptPubKey = CScript();
            pblock->vtx[0] = MakeTransactionRef(std::move(txCoinbase));
            if (txFirst.size() == 0)
                baseheight = ::ChainActive().Height();
            if (txFirst.size() < 4)
                txFirst.push_back(pblock->vtx[0]);
            pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);
            pblock->nNonce = blockinfo[i].nonce;
        }
        std::shared_ptr<const CBlock> shared_pblock = std::make_shared<const CBlock>(*pblock);
        BOOST_CHECK(Assert(m_node.chainman)->ProcessNewBlock(chainparams, shared_pblock, true, nullptr));
        pblock->hashPrevBlock = pblock->GetHash();
    }

    LOCK(cs_main);
    LOCK(m_node.mempool->cs);

    // Just to make sure we can still make simple blocks
    BOOST_CHECK(pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey));

    const CAmount BLOCKSUBSIDY = 50*COIN;
    const CAmount LOWFEE = CENT;
    const CAmount HIGHFEE = COIN;
    const CAmount HIGHERFEE = 4*COIN;

    // block sigops > limit: 1000 CHECKMULTISIG + 1
    tx.vin.resize(1);
    // NOTE: OP_NOP is used to force 20 SigOps for the CHECKMULTISIG
    tx.vin[0].scriptSig = CScript() << OP_0 << OP_0 << OP_0 << OP_NOP << OP_CHECKMULTISIG << OP_1;
    tx.vin[0].prevout.hash = txFirst[0]->GetHash();
    tx.vin[0].prevout.n = 0;
    tx.vout.resize(1);
    tx.vout[0].nValue = BLOCKSUBSIDY;
    for (unsigned int i = 0; i < 1001; ++i)
    {
        tx.vout[0].nValue -= LOWFEE;
        hash = tx.GetHash();
        bool spendsCoinbase = i == 0; // only first tx spends coinbase
        // If we don't set the # of sig ops in the CTxMemPoolEntry, template creation fails
        m_node.mempool->addUnchecked(entry.Fee(LOWFEE).Time(GetTime()).SpendsCoinbase(spendsCoinbase).FromTx(tx));
        tx.vin[0].prevout.hash = hash;
    }

    BOOST_CHECK_EXCEPTION(AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey), std::runtime_error, HasReason("bad-blk-sigops"));
    m_node.mempool->clear();

    tx.vin[0].prevout.hash = txFirst[0]->GetHash();
    tx.vout[0].nValue = BLOCKSUBSIDY;
    for (unsigned int i = 0; i < 1001; ++i)
    {
        tx.vout[0].nValue -= LOWFEE;
        hash = tx.GetHash();
        bool spendsCoinbase = i == 0; // only first tx spends coinbase
        // If we do set the # of sig ops in the CTxMemPoolEntry, template creation passes
        m_node.mempool->addUnchecked(entry.Fee(LOWFEE).Time(GetTime()).SpendsCoinbase(spendsCoinbase).SigOpsCost(80).FromTx(tx));
        tx.vin[0].prevout.hash = hash;
    }
    BOOST_CHECK(pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey));
    m_node.mempool->clear();

    // block size > limit
    tx.vin[0].scriptSig = CScript();
    // 18 * (520char + DROP) + OP_1 = 9433 bytes
    std::vector<unsigned char> vchData(520);
    for (unsigned int i = 0; i < 18; ++i)
        tx.vin[0].scriptSig << vchData << OP_DROP;
    tx.vin[0].scriptSig << OP_1;
    tx.vin[0].prevout.hash = txFirst[0]->GetHash();
    tx.vout[0].nValue = BLOCKSUBSIDY;
    for (unsigned int i = 0; i < 128; ++i)
    {
        tx.vout[0].nValue -= LOWFEE;
        hash = tx.GetHash();
        bool spendsCoinbase = i == 0; // only first tx spends coinbase
        m_node.mempool->addUnchecked(entry.Fee(LOWFEE).Time(GetTime()).SpendsCoinbase(spendsCoinbase).FromTx(tx));
        tx.vin[0].prevout.hash = hash;
    }
    BOOST_CHECK(pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey));
    m_node.mempool->clear();

    // orphan in *m_node.mempool, template creation fails
    hash = tx.GetHash();
    m_node.mempool->addUnchecked(entry.Fee(LOWFEE).Time(GetTime()).FromTx(tx));
    BOOST_CHECK_EXCEPTION(AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey), std::runtime_error, HasReason("bad-txns-inputs-missingorspent"));
    m_node.mempool->clear();

    // child with higher feerate than parent
    tx.vin[0].scriptSig = CScript() << OP_1;
    tx.vin[0].prevout.hash = txFirst[1]->GetHash();
    tx.vout[0].nValue = BLOCKSUBSIDY-HIGHFEE;
    hash = tx.GetHash();
    m_node.mempool->addUnchecked(entry.Fee(HIGHFEE).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    tx.vin[0].prevout.hash = hash;
    tx.vin.resize(2);
    tx.vin[1].scriptSig = CScript() << OP_1;
    tx.vin[1].prevout.hash = txFirst[0]->GetHash();
    tx.vin[1].prevout.n = 0;
    tx.vout[0].nValue = tx.vout[0].nValue+BLOCKSUBSIDY-HIGHERFEE; //First txn output + fresh coinbase - new txn fee
    hash = tx.GetHash();
    m_node.mempool->addUnchecked(entry.Fee(HIGHERFEE).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    BOOST_CHECK(pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey));
    m_node.mempool->clear();

    // coinbase in *m_node.mempool, template creation fails
    tx.vin.resize(1);
    tx.vin[0].prevout.SetNull();
    tx.vin[0].scriptSig = CScript() << OP_0 << OP_1;
    tx.vout[0].nValue = 0;
    hash = tx.GetHash();
    // give it a fee so it'll get mined
    m_node.mempool->addUnchecked(entry.Fee(LOWFEE).Time(GetTime()).SpendsCoinbase(false).FromTx(tx));
    // Should throw bad-cb-multiple
    BOOST_CHECK_EXCEPTION(AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey), std::runtime_error, HasReason("bad-cb-multiple"));
    m_node.mempool->clear();

    // double spend txn pair in *m_node.mempool, template creation fails
    tx.vin[0].prevout.hash = txFirst[0]->GetHash();
    tx.vin[0].scriptSig = CScript() << OP_1;
    tx.vout[0].nValue = BLOCKSUBSIDY-HIGHFEE;
    tx.vout[0].scriptPubKey = CScript() << OP_1;
    hash = tx.GetHash();
    m_node.mempool->addUnchecked(entry.Fee(HIGHFEE).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    tx.vout[0].scriptPubKey = CScript() << OP_2;
    hash = tx.GetHash();
    m_node.mempool->addUnchecked(entry.Fee(HIGHFEE).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    BOOST_CHECK_EXCEPTION(AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey), std::runtime_error, HasReason("bad-txns-inputs-missingorspent"));
    m_node.mempool->clear();

    // subsidy changing
    int nHeight = ::ChainActive().Height();
    // Create an actual 209999-long block chain (without valid blocks).
    while (::ChainActive().Tip()->nHeight < 209999) {
        CBlockIndex* prev = ::ChainActive().Tip();
        CBlockIndex* next = new CBlockIndex();
        next->phashBlock = new uint256(InsecureRand256());
        ::ChainstateActive().CoinsTip().SetBestBlock(next->GetBlockHash());
        next->pprev = prev;
        next->nHeight = prev->nHeight + 1;
        next->BuildSkip();
        ::ChainActive().SetTip(next);
    }
    BOOST_CHECK(pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey));
    // Extend to a 210000-long block chain.
    while (::ChainActive().Tip()->nHeight < 210000) {
        CBlockIndex* prev = ::ChainActive().Tip();
        CBlockIndex* next = new CBlockIndex();
        next->phashBlock = new uint256(InsecureRand256());
        ::ChainstateActive().CoinsTip().SetBestBlock(next->GetBlockHash());
        next->pprev = prev;
        next->nHeight = prev->nHeight + 1;
        next->BuildSkip();
        ::ChainActive().SetTip(next);
    }
    BOOST_CHECK(pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey));

    // invalid p2sh txn in *m_node.mempool, template creation fails
    tx.vin[0].prevout.hash = txFirst[0]->GetHash();
    tx.vin[0].prevout.n = 0;
    tx.vin[0].scriptSig = CScript() << OP_1;
    tx.vout[0].nValue = BLOCKSUBSIDY-LOWFEE;
    script = CScript() << OP_0;
    tx.vout[0].scriptPubKey = GetScriptForDestination(ScriptHash(script));
    hash = tx.GetHash();
    m_node.mempool->addUnchecked(entry.Fee(LOWFEE).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    tx.vin[0].prevout.hash = hash;
    tx.vin[0].scriptSig = CScript() << std::vector<unsigned char>(script.begin(), script.end());
    tx.vout[0].nValue -= LOWFEE;
    hash = tx.GetHash();
    m_node.mempool->addUnchecked(entry.Fee(LOWFEE).Time(GetTime()).SpendsCoinbase(false).FromTx(tx));
    // Should throw block-validation-failed
    BOOST_CHECK_EXCEPTION(AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey), std::runtime_error, HasReason("block-validation-failed"));
    m_node.mempool->clear();

    // Delete the dummy blocks again.
    while (::ChainActive().Tip()->nHeight > nHeight) {
        CBlockIndex* del = ::ChainActive().Tip();
        ::ChainActive().SetTip(del->pprev);
        ::ChainstateActive().CoinsTip().SetBestBlock(del->pprev->GetBlockHash());
        delete del->phashBlock;
        delete del;
    }

    // non-final txs in mempool
    // changed to 60 second block interval for consistency
    SetMockTime(::ChainActive().Tip()->GetMedianTimePast()+60);
    int flags = LOCKTIME_VERIFY_SEQUENCE|LOCKTIME_MEDIAN_TIME_PAST;
    // height map
    std::vector<int> prevheights;

    // relative height locked
    tx.nVersion = 2;
    tx.vin.resize(1);
    prevheights.resize(1);
    tx.vin[0].prevout.hash = txFirst[0]->GetHash(); // only 1 transaction
    tx.vin[0].prevout.n = 0;
    tx.vin[0].scriptSig = CScript() << OP_1;
    tx.vin[0].nSequence = ::ChainActive().Tip()->nHeight + 1; // txFirst[0] is the 2nd block
    prevheights[0] = baseheight + 1;
    tx.vout.resize(1);
    tx.vout[0].nValue = BLOCKSUBSIDY-HIGHFEE;
    tx.vout[0].scriptPubKey = CScript() << OP_1;
    tx.nLockTime = 0;
    hash = tx.GetHash();
    m_node.mempool->addUnchecked(entry.Fee(HIGHFEE).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    BOOST_CHECK(CheckFinalTx(CTransaction(tx), flags)); // Locktime passes
    BOOST_CHECK(!TestSequenceLocks(CTransaction(tx), flags)); // Sequence locks fail
    BOOST_CHECK(SequenceLocks(CTransaction(tx), flags, prevheights, CreateBlockIndex(::ChainActive().Tip()->nHeight + 2))); // Sequence locks pass on 2nd block

    // relative time locked
    tx.vin[0].prevout.hash = txFirst[1]->GetHash();
    tx.vin[0].nSequence = CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG | (((::ChainActive().Tip()->GetMedianTimePast()+1-::ChainActive()[1]->GetMedianTimePast()) >> CTxIn::SEQUENCE_LOCKTIME_GRANULARITY) + 1); // txFirst[1] is the 3rd block
    prevheights[0] = baseheight + 2;
    hash = tx.GetHash();
    m_node.mempool->addUnchecked(entry.Time(GetTime()).FromTx(tx));
    BOOST_CHECK(CheckFinalTx(CTransaction(tx), flags)); // Locktime passes
    BOOST_CHECK(!TestSequenceLocks(CTransaction(tx), flags)); // Sequence locks fail

    for (int i = 0; i < CBlockIndex::nMedianTimeSpan; i++)
        ::ChainActive().Tip()->GetAncestor(::ChainActive().Tip()->nHeight - i)->nTime += 512; //Trick the MedianTimePast
    BOOST_CHECK(SequenceLocks(CTransaction(tx), flags, prevheights, CreateBlockIndex(::ChainActive().Tip()->nHeight + 1))); // Sequence locks pass 512 seconds later
    for (int i = 0; i < CBlockIndex::nMedianTimeSpan; i++)
        ::ChainActive().Tip()->GetAncestor(::ChainActive().Tip()->nHeight - i)->nTime -= 512; //undo tricked MTP

    // absolute height locked
    tx.vin[0].prevout.hash = txFirst[2]->GetHash();
    tx.vin[0].nSequence = CTxIn::SEQUENCE_FINAL - 1;
    prevheights[0] = baseheight + 3;
    tx.nLockTime = ::ChainActive().Tip()->nHeight + 1;
    hash = tx.GetHash();
    m_node.mempool->addUnchecked(entry.Time(GetTime()).FromTx(tx));
    BOOST_CHECK(!CheckFinalTx(CTransaction(tx), flags)); // Locktime fails
    BOOST_CHECK(TestSequenceLocks(CTransaction(tx), flags)); // Sequence locks pass
    BOOST_CHECK(IsFinalTx(CTransaction(tx), ::ChainActive().Tip()->nHeight + 2, ::ChainActive().Tip()->GetMedianTimePast())); // Locktime passes on 2nd block

    // absolute time locked
    tx.vin[0].prevout.hash = txFirst[3]->GetHash();
    tx.nLockTime = ::ChainActive().Tip()->GetMedianTimePast();
    prevheights.resize(1);
    prevheights[0] = baseheight + 4;
    hash = tx.GetHash();
    m_node.mempool->addUnchecked(entry.Time(GetTime()).FromTx(tx));
    BOOST_CHECK(!CheckFinalTx(CTransaction(tx), flags)); // Locktime fails
    BOOST_CHECK(TestSequenceLocks(CTransaction(tx), flags)); // Sequence locks pass
    BOOST_CHECK(IsFinalTx(CTransaction(tx), ::ChainActive().Tip()->nHeight + 2, ::ChainActive().Tip()->GetMedianTimePast() + 1)); // Locktime passes 1 second later

    // mempool-dependent transactions (not added)
    tx.vin[0].prevout.hash = hash;
    prevheights[0] = ::ChainActive().Tip()->nHeight + 1;
    tx.nLockTime = 0;
    tx.vin[0].nSequence = 0;
    BOOST_CHECK(CheckFinalTx(CTransaction(tx), flags)); // Locktime passes
    BOOST_CHECK(TestSequenceLocks(CTransaction(tx), flags)); // Sequence locks pass
    tx.vin[0].nSequence = 1;
    BOOST_CHECK(!TestSequenceLocks(CTransaction(tx), flags)); // Sequence locks fail
    tx.vin[0].nSequence = CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG;
    BOOST_CHECK(TestSequenceLocks(CTransaction(tx), flags)); // Sequence locks pass
    tx.vin[0].nSequence = CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG | 1;
    BOOST_CHECK(!TestSequenceLocks(CTransaction(tx), flags)); // Sequence locks fail

    BOOST_CHECK(pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey));

    // None of the of the absolute height/time locked tx should have made
    // it into the template because we still check IsFinalTx in CreateNewBlock,
    // but relative locked txs will if inconsistently added to mempool.
    // For now these will still generate a valid template until BIP68 soft fork
    BOOST_CHECK_EQUAL(pblocktemplate->block.vtx.size(), 3U);
    // However if we advance height by 1 and time by 512, all of them should be mined
    for (int i = 0; i < CBlockIndex::nMedianTimeSpan; i++)
        ::ChainActive().Tip()->GetAncestor(::ChainActive().Tip()->nHeight - i)->nTime += 512; //Trick the MedianTimePast
    ::ChainActive().Tip()->nHeight++;
    // changed to 60 second block interval for consistency
    SetMockTime(::ChainActive().Tip()->GetMedianTimePast() + 60);

    BOOST_CHECK(pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey));
    BOOST_CHECK_EQUAL(pblocktemplate->block.vtx.size(), 5U);


    ::ChainActive().Tip()->nHeight--;
    SetMockTime(0);
    m_node.mempool->clear();

    TestPackageSelection(chainparams, scriptPubKey, txFirst);

    fCheckpointsEnabled = true;
}

BOOST_AUTO_TEST_SUITE_END()
