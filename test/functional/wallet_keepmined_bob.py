#!/usr/bin/env python3
# Copyright (c) 2026 The Dobbscoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""(BOB) keep solo-mined blocks intact, and keep old coins their age.

node1 mines 25 blocks to one address (25 coinbase coins, all "small" under the test's
-consolidatebelow) and also holds a few ordinary coins. By default (-keepminedblocks=1):
  - automatic merging never touches the mined blocks;
  - an ordinary send is paid from ordinary coins only, however small the mined blocks are;
  - a send the ordinary coins cannot cover does go through, using mined blocks;
  - a mined block picked by hand is spent as asked.
With -keepminedblocks=0 the mined blocks are coins like any other and get merged.
-consolidatekeepbefore: coins that confirmed before the cutoff are never merged, coins
from after it are.
"""
from decimal import Decimal
import time

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal

ARGS = ["-consolidategap=0", "-consolidatemin=20", "-consolidatebatch=15", "-consolidatebelow=100000"]


class KeepMinedBobTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [[], ARGS]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def tick(self):
        self.nodes[1].mockscheduler(61)

    def merges(self):
        seen, out = set(), []
        for t in self.nodes[1].listtransactions("*", 1000):
            if t.get("comment", "").startswith("Automatic maintenance: merged") and t["txid"] not in seen:
                seen.add(t["txid"])
                out.append(t)
        return out

    def is_coinbase(self, txid):
        return "coinbase" in self.nodes[1].gettransaction(txid, True, True)["decoded"]["vin"][0]

    def mined_inputs(self, txid):
        tx = self.nodes[1].gettransaction(txid, True, True)["decoded"]
        return sum(1 for i in tx["vin"] if self.is_coinbase(i["txid"])), len(tx["vin"])

    def confirm(self):
        self.sync_mempools()
        self.nodes[0].generatetoaddress(1, self.nodes[0].getnewaddress())
        self.sync_all()

    def restart1(self, extra):
        self.restart_node(1, ARGS + extra)
        self.connect_nodes(0, 1)

    def run_test(self):
        n0, n1 = self.nodes
        mine_addr = n1.getnewaddress("my blocks")
        n1.generatetoaddress(25, mine_addr)
        self.sync_all()
        n0.generatetoaddress(110, n0.getnewaddress())   # node1's 25 blocks mature, node0 gets funds
        self.sync_all()
        mined = [u for u in n1.listunspent() if u["address"] == mine_addr]
        assert_equal(len(mined), 25)
        assert all(self.is_coinbase(u["txid"]) for u in mined)
        block_value = mined[0]["amount"]

        self.log.info("merging leaves 25 mined blocks alone (well over the threshold of 20)")
        self.tick()
        self.tick()
        assert_equal(self.merges(), [])
        assert_equal(n1.getmempoolinfo()["size"], 0)

        self.log.info("an ordinary send uses ordinary coins, not mined blocks")
        for amount in (3, 2, 2):
            n0.sendtoaddress(n1.getnewaddress(), amount)
        self.confirm()
        txid = n1.sendtoaddress(n0.getnewaddress(), 4)
        assert_equal(self.mined_inputs(txid)[0], 0)
        self.confirm()

        self.log.info("a send the ordinary coins cannot cover uses mined blocks")
        plain = sum(u["amount"] for u in n1.listunspent() if not self.is_coinbase(u["txid"]))
        assert plain < 4, plain
        txid = n1.sendtoaddress(n0.getnewaddress(), plain + block_value)
        n_mined, n_inputs = self.mined_inputs(txid)
        assert n_mined >= 1, (n_mined, n_inputs)
        self.confirm()

        self.log.info("a mined block picked by hand is spent as asked")
        pick = [u for u in n1.listunspent() if u["address"] == mine_addr][0]
        n0.sendtoaddress(n1.getnewaddress(), 50)        # plenty of ordinary coin as well
        self.confirm()
        raw = n1.createrawtransaction([{"txid": pick["txid"], "vout": pick["vout"]}], {n0.getnewaddress(): Decimal("0.5")})
        funded = n1.fundrawtransaction(raw, {"add_inputs": False})
        txid = n1.sendrawtransaction(n1.signrawtransactionwithwallet(funded["hex"])["hex"])
        assert_equal(self.mined_inputs(txid), (1, 1))
        self.confirm()

        left = len([u for u in n1.listunspent() if u["address"] == mine_addr])
        self.log.info("%d mined blocks left; still no merge" % left)
        assert left > 20, left
        self.tick()
        assert_equal(self.merges(), [])

        self.log.info("-keepminedblocks=0: mined blocks are coins like any other and get merged")
        self.restart1(["-keepminedblocks=0"])
        self.tick()
        self.wait_until(lambda: len(self.merges()) == 1)
        n_mined, n_inputs = self.mined_inputs(self.merges()[0]["txid"])
        assert_equal((n_mined, n_inputs), (15, 15))
        self.confirm()

        self.log.info("coins from before the cutoff keep their age: never merged")
        self.restart1(["-consolidatekeepbefore=%d" % (int(time.time()) + 365 * 86400)])
        payout = n1.getnewaddress("pool payouts")
        for i in range(25):
            n0.sendtoaddress(payout, Decimal("0.5") + Decimal(i) / 100)
        self.confirm()
        self.tick()
        self.tick()
        assert_equal(len(self.merges()), 1)
        assert_equal(n1.getmempoolinfo()["size"], 0)

        self.log.info("coins from after the cutoff are merged, mined blocks still are not")
        self.restart1(["-consolidatekeepbefore=%d" % (int(time.time()) - 3600)])
        self.tick()
        self.wait_until(lambda: len(self.merges()) == 2)
        n_mined, n_inputs = self.mined_inputs(self.merges()[-1]["txid"])
        assert_equal((n_mined, n_inputs), (0, 15))


if __name__ == '__main__':
    KeepMinedBobTest().main()
