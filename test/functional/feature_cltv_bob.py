#!/usr/bin/env python3
# Wallet-free CLTV (BIP65 / OP_CHECKLOCKTIMEVERIFY) activation test for the (BOB)
# Dogecoin-Core rebase. Part of the block-2,000,000-fork harness: mainnet activates
# CLTV at HARDFORK_CLTV_MAIN=2000000, which the from-genesis acid test can't reach,
# so we exercise the activation on regtest (BIP65Height=1351) instead.
#
# Fully self-contained: every block is built with the framework and submitted via
# submitblock. Coinbases are OP_TRUE (anyone-can-spend), so spends need no wallet and
# no signing. The classic minimal probe: prepend [OP_1NEGATE CLTV OP_DROP] to a spend's
# scriptSig. Pre-activation CLTV is a NOP -> the -1 is dropped -> valid. Post-activation
# CLTV enforces -> negative locktime -> invalid.

from test_framework.blocktools import create_coinbase, create_block
from test_framework.messages import CTransaction, CTxIn, CTxOut, COutPoint
from test_framework.script import (CScript, CScriptNum, OP_1NEGATE,
                                   OP_CHECKLOCKTIMEVERIFY, OP_DROP, OP_TRUE)
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal

CLTV_HEIGHT = 1351   # regtest consensus.BIP65Height

class BobCltvTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [['-par=1']]

    def run_test(self):
        node = self.nodes[0]
        self.tip = int(node.getbestblockhash(), 16)
        self.height = 1
        self.block_time = node.getblock(node.getbestblockhash())['time'] + 1

        def mine(txs=None):
            cb = create_coinbase(self.height)                       # OP_TRUE coinbase
            # create_coinbase pays Doge's regtest subsidy (500000 COIN); the (BOB) reward
            # curve allows far less, so pay a flat 1 COIN (<= (BOB) subsidy at every height;
            # a coinbase may underpay). Avoids bad-cb-amount.
            cb.vout[0].nValue = 100000000
            cb.rehash()          # force txid recompute (calc_sha256 no-ops once sha256 is set)
            # version 4 satisfies BIP34(>=2 @1000) / BIP66(>=3 @1251) / BIP65(>=4 @1351)
            block = create_block(self.tip, cb, self.block_time, version=4, txlist=(txs or []))
            block.solve()
            res = node.submitblock(block.serialize().hex())
            return block, res

        def spend(coinbase_tx, script_sig_prefix, nseq=0xffffffff, nlock=0):
            tx = CTransaction()
            tx.vin = [CTxIn(COutPoint(coinbase_tx.sha256, 0), CScript(script_sig_prefix), nseq)]
            tx.vout = [CTxOut(coinbase_tx.vout[0].nValue - 1000, CScript([OP_TRUE]))]
            tx.nLockTime = nlock
            tx.rehash()
            return tx

        # 1) mine up to CLTV_HEIGHT-2, banking spendable OP_TRUE coinbases
        self.log.info("mining %d blocks to height %d" % (CLTV_HEIGHT - 2, CLTV_HEIGHT - 2))
        coinbases = []
        while self.height <= CLTV_HEIGHT - 2:
            block, res = mine()
            assert res is None, "unexpected reject mining block %d: %s" % (self.height, res)
            coinbases.append(block.vtx[0])
            self.tip = block.sha256
            self.block_time += 1
            self.height += 1
        assert_equal(node.getblockcount(), CLTV_HEIGHT - 2)

        invalid_prefix = [OP_1NEGATE, OP_CHECKLOCKTIMEVERIFY, OP_DROP]   # -1 -> fails when enforced

        # 2) height CLTV_HEIGHT-1 (pre-activation): CLTV is a NOP -> block ACCEPTED
        block, res = mine([spend(coinbases[0], invalid_prefix)])
        assert res is None, "PRE-activation block (CLTV as NOP) wrongly rejected: %s" % res
        assert_equal(node.getbestblockhash(), '%064x' % block.sha256)
        self.log.info("height %d PRE-activation: CLTV treated as NOP, accepted  [OK]" % self.height)
        self.tip = block.sha256; self.block_time += 1; self.height += 1

        # 3) height CLTV_HEIGHT (activation): same invalid-CLTV tx -> block REJECTED
        bad_block, res = mine([spend(coinbases[1], invalid_prefix)])
        assert res is not None, "CLTV NOT enforced at height %d - invalid block was ACCEPTED" % self.height
        assert node.getbestblockhash() != '%064x' % bad_block.sha256
        self.log.info("height %d ACTIVATION: invalid CLTV rejected (%s)  [OK]" % (self.height, res))

        # 4) height CLTV_HEIGHT: a SATISFIED CLTV spend -> block ACCEPTED.
        #    lock = height-1 so CLTV passes (nLockTime >= stack) AND the tx is final at
        #    this block height (nLockTime < height); nSequence != final so CLTV engages.
        lock = self.height - 1
        valid_prefix = [CScriptNum(lock), OP_CHECKLOCKTIMEVERIFY, OP_DROP]
        good = spend(coinbases[2], valid_prefix, nseq=0, nlock=lock)
        block, res = mine([good])
        assert res is None, "satisfied CLTV spend wrongly rejected post-activation: %s" % res
        assert_equal(node.getbestblockhash(), '%064x' % block.sha256)
        self.log.info("height %d POST-activation: satisfied CLTV accepted  [OK]" % self.height)

        self.log.info("PASS: CLTV (BIP65) activates and enforces correctly at height %d on the (BOB) rebase" % CLTV_HEIGHT)

if __name__ == '__main__':
    BobCltvTest().main()
