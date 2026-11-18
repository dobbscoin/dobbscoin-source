#!/usr/bin/env python3
# Copyright (c) 2026 The Dobbscoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""(BOB) the built-in CPU miner (setgenerate / getgenerate / gethashespersec), from 0.14.

On regtest each miner thread stops after the block it finds. For each scrypt
implementation the CPU supports (-minerscrypt), mine with one thread and then
two; the blocks must be accepted, pay the wallet (a new address per block), and
the miner must report itself stopped afterwards. Mining without a wallet loaded
is refused.
"""

import os
import time

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error


class BobMinerTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def wait_mined(self, node, height, timeout=120):
        deadline = time.time() + timeout
        while time.time() < deadline:
            if node.getblockcount() >= height and not node.getgenerate():
                return
            time.sleep(0.25)
        raise AssertionError("miner did not reach height %d (at %d, generating=%s)" % (height, node.getblockcount(), node.getgenerate()))

    def run_test(self):
        node = self.nodes[0]
        assert_equal(node.getgenerate(), False)
        assert_equal(node.gethashespersec(), 0)

        for impl in ["generic", "sse2", "avx", "avx2"]:
            self.restart_node(0, ["-minerscrypt=" + impl])
            node = self.nodes[0]
            with open(os.path.join(node.datadir, self.chain, "debug.log"), encoding="utf8") as f:
                log = f.read()
            if "-minerscrypt=%s is not supported" % impl in log:
                self.log.info("%s: not supported by this CPU, skipped" % impl)
                continue
            assert "(forced by -minerscrypt)" in log
            for threads in [1, 2]:
                start = node.getblockcount()
                node.setgenerate(True, threads)
                self.wait_mined(node, start + threads)
                assert_equal(node.getblockcount(), start + threads)
                payees = set()
                for h in range(start + 1, start + threads + 1):
                    block = node.getblock(node.getblockhash(h), 2)
                    addr = block["tx"][0]["vout"][0]["scriptPubKey"]["addresses"][0]
                    assert node.getaddressinfo(addr)["ismine"], "coinbase does not pay the wallet"
                    payees.add(addr)
                assert_equal(len(payees), threads)  # a new address per block
                self.log.info("%s: %d thread(s) mined %d block(s), each to a new wallet address" % (impl, threads, threads))

        self.log.info("setgenerate false stops a running miner")
        node.setgenerate(True, 1)
        node.setgenerate(False)
        assert_equal(node.getgenerate(), False)

        self.log.info("mining needs a wallet for the coinbase")
        node.unloadwallet(self.default_wallet_name)
        assert_raises_rpc_error(-18, "No wallet is loaded", node.setgenerate, True, 1)

        self.log.info("blocks mined by the built-in miner survive a restart")
        height = node.getblockcount()
        self.restart_node(0)
        assert_equal(self.nodes[0].getblockcount(), height)


if __name__ == '__main__':
    BobMinerTest().main()
