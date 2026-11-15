#!/usr/bin/env python3
# (BOB) merge-mining RPC test: createauxblock / submitauxblock on the 0.21 rebase.
# Mirrors the (BOB) 0.14 qa/auxpow-smoke/regtest-smoke.sh contract (tests 1-6),
# then merge-mines real blocks through the RPC. Regtest AuxPoW activates at 10.

import struct

from test_framework.address import byte_to_base58
from test_framework.messages import (CAuxPow, CBlockHeader, COutPoint, CTransaction, CTxIn,
                                     CTxOut, VERSION_AUXPOW, ser_uint256, uint256_from_compact)
from test_framework.script import CScript, OP_TRUE, hash160
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error

AUXPOW_FORK = 10
BOB_CHAIN_ID = 0x00B0
MAGIC = b'\xfa\xbe' + b'mm'
RPC_INVALID_PARAMETER = -8
RPC_DESERIALIZATION_ERROR = -22
RPC_METHOD_NOT_FOUND = -32601


def make_auxpow(block_hash_hex, bits_hex, parent_valid=True):
    """Parent coinbase committing to the aux block hash; parent PoW ground to (miss) target."""
    root = bytes.fromhex(block_hash_hex)                      # display order = byte-reversed
    payload = MAGIC + root + struct.pack("<II", 1, 0)
    cb = CTransaction()
    cb.vin = [CTxIn(COutPoint(0, 0xffffffff), CScript([payload]), 0xffffffff)]
    cb.vout = [CTxOut(0, CScript([OP_TRUE]))]
    cb.rehash()
    ap = CAuxPow()
    ap.nVersion, ap.vin, ap.vout, ap.nLockTime = cb.nVersion, cb.vin, cb.vout, cb.nLockTime
    ap.hashBlock, ap.vMerkleBranch, ap.nIndex = 0, [], 0
    ap.vChainMerkleBranch, ap.nChainIndex = [], 0
    p = CBlockHeader()
    p.nVersion = 2
    p.hashPrevBlock, p.hashMerkleRoot, p.nTime, p.nBits, p.nNonce = 0, cb.sha256, 1, int(bits_hex, 16), 0
    target = uint256_from_compact(p.nBits)
    p.rehash()
    while (p.scrypt256 <= target) != parent_valid:
        p.nNonce += 1
        p.rehash()
    ap.parentBlock = p
    return ap.serialize().hex()


class BobAuxpowRpcTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def run_test(self):
        node = self.nodes[0]
        addr = byte_to_base58(hash160(b'\x02' + b'\x11' * 32), 111)
        assert node.validateaddress(addr)['isvalid']
        desc = node.getdescriptorinfo("raw(51)")['descriptor']

        self.log.info("1. createauxblock refuses before activation")
        assert_raises_rpc_error(RPC_METHOD_NOT_FOUND, "AuxPoW activation height not yet reached",
                                node.createauxblock, addr)
        node.generatetodescriptor(AUXPOW_FORK - 1, desc)
        assert_equal(node.getblockcount(), AUXPOW_FORK - 1)

        self.log.info("2. response has the 7 documented fields, chainid 176")
        t = node.createauxblock(addr)
        assert_equal(sorted(t), sorted(["hash", "chainid", "previousblockhash", "coinbasevalue",
                                        "bits", "height", "target"]))
        assert_equal(t["chainid"], BOB_CHAIN_ID)
        assert_equal(t["height"], AUXPOW_FORK)
        assert_equal(t["previousblockhash"], node.getbestblockhash())
        assert_equal(t["bits"], "207fffff")
        assert_equal(t["coinbasevalue"], 50 * 100000000)     # (BOB) testnet/regtest curve, height 10
        target = uint256_from_compact(int(t["bits"], 16))
        assert_equal(t["target"], ser_uint256(target).hex())  # reversed byte order, like 0.14

        self.log.info("3. submitauxblock rejects an unknown block hash")
        assert_raises_rpc_error(RPC_INVALID_PARAMETER, "block hash unknown",
                                node.submitauxblock, "11" * 32, make_auxpow(t["hash"], t["bits"]))

        self.log.info("4. submitauxblock rejects malformed auxpow hex")
        assert_raises_rpc_error(RPC_DESERIALIZATION_ERROR, "AuxPow decode failed",
                                node.submitauxblock, t["hash"], "zz")
        assert_raises_rpc_error(RPC_DESERIALIZATION_ERROR, "AuxPow decode failed",
                                node.submitauxblock, t["hash"], "00")
        assert_raises_rpc_error(RPC_INVALID_PARAMETER, "Invalid coinbase payout address",
                                node.createauxblock, "notanaddress")

        self.log.info("5. repeat createauxblock: fresh template, earlier one stays submittable")
        t2 = node.createauxblock(addr)
        assert t2["hash"] != t["hash"]
        assert_equal(t2["previousblockhash"], t["previousblockhash"])

        self.log.info("   a failed proof does not poison the cached template")
        assert_equal(node.submitauxblock(t["hash"], make_auxpow(t["hash"], t["bits"], parent_valid=False)), False)
        assert_equal(node.getblockcount(), AUXPOW_FORK - 1)
        assert_equal(node.submitauxblock(t["hash"], make_auxpow(t["hash"], t["bits"])), True)
        assert_equal(node.getbestblockhash(), t["hash"])
        blk = node.getblock(t["hash"])
        assert_equal(blk["height"], AUXPOW_FORK)
        assert int(blk["versionHex"], 16) & VERSION_AUXPOW
        assert_equal(int(blk["versionHex"], 16) >> 16, BOB_CHAIN_ID)

        self.log.info("6. advancing the tip invalidates the cache")
        stale = t2["hash"]
        t3 = node.createauxblock(addr)                         # new tip -> cache cleared
        assert_equal(t3["height"], AUXPOW_FORK + 1)
        assert_raises_rpc_error(RPC_INVALID_PARAMETER, "block hash unknown",
                                node.submitauxblock, stale, make_auxpow(stale, t2["bits"]))

        self.log.info("merge-mine 5 more blocks through the RPC")
        for _ in range(5):
            t = node.createauxblock(addr)
            assert_equal(node.submitauxblock(t["hash"], make_auxpow(t["hash"], t["bits"])), True)
            assert_equal(node.getbestblockhash(), t["hash"])
        assert_equal(node.getblockcount(), AUXPOW_FORK + 5)

        self.log.info("restart: RPC-mined blocks read back")
        tip = node.getbestblockhash()
        self.restart_node(0)
        assert_equal(self.nodes[0].getbestblockhash(), tip)
        assert self.nodes[0].verifychain(4, 0)
        self.log.info("PASS: createauxblock/submitauxblock match the (BOB) 0.14 contract")


if __name__ == '__main__':
    BobAuxpowRpcTest().main()
