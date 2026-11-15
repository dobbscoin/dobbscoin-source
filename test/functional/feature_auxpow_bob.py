#!/usr/bin/env python3
# Wallet-free AuxPoW (merge-mining) activation test for the (BOB) Dogecoin-Core
# rebase. Second half of the block-2,000,000-fork harness: mainnet turns on
# AuxPoW at HARDFORK_AUXPOW_MAIN=2000000 (chain ID 0x00B0), which the
# from-genesis acid test cannot reach, so the fork runs here on regtest with
# -auxpowheight lowered.
#
# Every rule asserted below is the (BOB) 0.14 daemon's rule (src/main.cpp
# ContextualCheckBlockHeader, src/pow.cpp CheckAuxPowProofOfWork,
# src/auxpow.cpp CAuxPow::check), because that is the code the network runs
# at the fork. The fork height sits past regtest BIP65Height so the test also
# covers the base-version-3 blocks 0.14 miners keep producing after CLTV.

import struct

from test_framework.blocktools import create_coinbase, create_block
from test_framework.messages import (CAuxPow, CBlockHeader, COutPoint, CTransaction,
                                     CTxIn, CTxOut, VERSION_AUXPOW, VERSION_CHAIN_START,
                                     ser_uint256, uint256_from_compact)
from test_framework.script import CScript, OP_TRUE
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal

BOB_CHAIN_ID = 0x00B0
FORK = 1400          # -auxpowheight; past regtest BIP65Height (150) and BIP66Height (1251)
BIP65_HEIGHT = 150
MM_MAGIC = b'\xfa\xbe' + b'mm'


def version(base, chain_id=0, auxpow=False):
    return base + chain_id * VERSION_CHAIN_START + (VERSION_AUXPOW if auxpow else 0)


class BobAuxpowTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [['-par=1', '-auxpowheight=%d' % FORK]]

    # ---- block building ---------------------------------------------------
    def new_block(self, nversion, ntime_bump=0):
        cb = create_coinbase(self.height)
        cb.vout[0].nValue = 100000000          # 1 COIN, under the (BOB) subsidy cap
        cb.rehash()
        block = create_block(self.tip, cb, self.block_time + ntime_bump)
        block.nVersion = nversion
        block.rehash()
        return block

    def solve_own(self, block, want_valid=True):
        """Grind the header nonce until its OWN scrypt hash is (or is not) under target."""
        target = uint256_from_compact(block.nBits)
        block.rehash()
        while (block.scrypt256 <= target) != want_valid:
            block.nNonce += 1
            block.rehash()

    def attach_auxpow(self, block, *, parent_valid=True, parent_chain_id=0,
                      n_index=0, magic_twice=False, wrong_root=False,
                      chain_index=0):
        """Merge-mine `block`: build a parent coinbase that commits to its hash,
        then grind a parent header whose scrypt hash meets (or misses) nBits."""
        assert block.nVersion & VERSION_AUXPOW
        block.rehash()
        root = block.sha256 ^ (1 if wrong_root else 0)
        commitment = MM_MAGIC + ser_uint256(root)[::-1] + struct.pack('<II', 1, 0)
        if magic_twice:
            commitment += MM_MAGIC
        coinbase = CTransaction()
        coinbase.vin = [CTxIn(COutPoint(0, 0xffffffff), CScript([commitment]), 0xffffffff)]
        coinbase.vout = [CTxOut(0, CScript([OP_TRUE]))]
        coinbase.rehash()

        auxpow = CAuxPow()
        auxpow.vin, auxpow.vout, auxpow.nLockTime = coinbase.vin, coinbase.vout, coinbase.nLockTime
        auxpow.nVersion = coinbase.nVersion
        auxpow.hashBlock = 0
        auxpow.vMerkleBranch = []
        auxpow.nIndex = n_index
        auxpow.vChainMerkleBranch = []
        auxpow.nChainIndex = chain_index

        parent = CBlockHeader()
        parent.nVersion = version(2, parent_chain_id)
        parent.hashPrevBlock = 0
        parent.hashMerkleRoot = coinbase.sha256
        parent.nTime = block.nTime
        parent.nBits = block.nBits
        parent.nNonce = 0
        target = uint256_from_compact(parent.nBits)
        parent.rehash()
        while (parent.scrypt256 <= target) != parent_valid:
            parent.nNonce += 1
            parent.rehash()
        auxpow.parentBlock = parent
        block.auxpow = auxpow
        return block

    def submit(self, block):
        return self.node.submitblock(block.serialize().hex())

    def accept(self, block, what):
        res = self.submit(block)
        assert res is None, "%s wrongly rejected at height %d: %s" % (what, self.height, res)
        assert_equal(self.node.getbestblockhash(), block.hash)
        self.log.info("  height %d ACCEPT  %s" % (self.height, what))
        self.tip = block.sha256
        self.block_time += 1
        self.height += 1

    def reject(self, block, what, reason, log=None):
        if log:
            with self.node.assert_debug_log([log]):
                res = self.submit(block)
        else:
            res = self.submit(block)
        assert_equal(res, reason)
        assert self.node.getbestblockhash() != block.hash
        self.log.info("  height %d REJECT  %s (%s)" % (self.height, what, res))

    # ---- the test ---------------------------------------------------------
    def run_test(self):
        self.node = self.nodes[0]
        self.tip = int(self.node.getbestblockhash(), 16)
        self.height = 1
        self.block_time = self.node.getblock(self.node.getbestblockhash())['time'] + 1

        self.log.info("mining legacy blocks to height %d (base 4 below BIP65, base 3 after)" % (FORK - 2))
        while self.height <= FORK - 2:
            base = 4 if self.height < BIP65_HEIGHT else 3
            block = self.new_block(version(base))
            block.solve()
            res = self.submit(block)
            assert res is None, "legacy block %d rejected: %s" % (self.height, res)
            self.tip = block.sha256
            self.block_time += 1
            self.height += 1
        self.log.info("  base-3 blocks past BIP65Height accepted (0.14 miners stamp v3)  [OK]")

        self.log.info("height %d: last pre-fork block" % self.height)
        b = self.new_block(version(3, BOB_CHAIN_ID)); b.solve()
        self.reject(b, "chain ID 0x00B0 before the fork", "premature-chainid")
        b = self.attach_auxpow(self.new_block(version(3, BOB_CHAIN_ID, auxpow=True), 1))
        self.reject(b, "merge-mined block before the fork", "premature-auxpow")
        b = self.new_block(version(3)); b.solve()
        self.accept(b, "legacy chain-ID-0 block")

        self.log.info("height %d: AuxPoW activation height" % self.height)
        b = self.new_block(version(3)); b.solve()
        self.reject(b, "legacy chain-ID-0 block after the fork", "bad-chainid")
        b = self.new_block(version(3, 0x0062)); b.solve()
        self.reject(b, "Dogecoin chain ID 0x0062", "bad-chainid")
        b = self.new_block(version(2, BOB_CHAIN_ID)); b.solve()
        self.reject(b, "base version 2 past BIP66Height", "bad-version(0x%08x)" % b.nVersion)

        def mm(bump, **kw):
            blk = self.new_block(version(3, BOB_CHAIN_ID, auxpow=True), bump)
            self.solve_own(blk, want_valid=kw.pop('own_valid', False))
            return self.attach_auxpow(blk, **kw)

        self.reject(mm(2, own_valid=True, parent_valid=False),
                    "parent misses target (own header would pass)", "high-hash")
        self.reject(mm(3, parent_chain_id=BOB_CHAIN_ID),
                    "parent carries our chain ID", "high-hash", "Aux POW parent has our chain ID")
        self.reject(mm(4, n_index=1),
                    "parent coinbase nIndex != 0", "high-hash", "Aux POW is not a generate")
        self.reject(mm(5, wrong_root=True),
                    "coinbase commits to another block", "high-hash",
                    "Aux POW missing chain merkle root in parent coinbase")
        self.reject(mm(6, magic_twice=True),
                    "merged-mining header twice", "high-hash", "Multiple merged mining headers in coinbase")
        self.reject(mm(7, chain_index=1),
                    "wrong chain merkle index", "high-hash", "Aux POW wrong index")

        b = mm(8)        # own scrypt hash MISSES target; only the parent's PoW is valid
        self.accept(b, "merge-mined block, PoW from the parent only")
        mm_hash = b.hash

        self.log.info("height %d: solo-mined block after the fork" % self.height)
        b = self.new_block(version(3, BOB_CHAIN_ID)); b.solve()
        self.accept(b, "solo block with chain ID 0x00B0, base version 3")
        b = mm(1)
        self.accept(b, "second merge-mined block")

        self.log.info("restart: merge-mined blocks must read back from disk")
        tip = self.node.getbestblockhash()
        self.restart_node(0)
        assert_equal(self.node.getbestblockhash(), tip)
        assert self.node.getblock(mm_hash)['height'] == FORK
        assert self.node.verifychain(4, 0)
        self.log.info("  tip + merge-mined block read back, verifychain level 4 OK  [OK]")

        self.log.info("PASS: AuxPoW activates at %d with (BOB) 0.14 rules (chain ID 0x00B0)" % FORK)


if __name__ == '__main__':
    BobAuxpowTest().main()
