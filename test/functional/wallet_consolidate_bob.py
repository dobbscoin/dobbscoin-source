#!/usr/bin/env python3
# Copyright (c) 2026 The Dobbscoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""(BOB) #55 part B: automatic consolidation of small coins.

node1 receives many small payouts on one address (like pool payouts), plus a few on
other addresses. With small thresholds (-consolidatemin=20 -consolidatebatch=15
-consolidatebelow=2) the background pass (driven here with mockscheduler) must:
  - do nothing while it is turned off, at or under the threshold, or while the wallet
    is still catching up with the chain;
  - merge the 15 smallest coins of the busiest address into one coin on that same
    address, paying only a fee, and never touch the other addresses;
  - send no second batch until the first one confirms, then send the next one;
  - wait while the wallet is locked, and run once it is unlocked;
  - with -consolidateall, merge across addresses.
"""
from decimal import Decimal

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal

ARGS = ["-consolidategap=0", "-consolidatemin=20", "-consolidatebatch=15", "-consolidatebelow=2"]


class ConsolidateBobTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [[], ARGS + ["-consolidate=0"]]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def tick(self):
        """One background pass on node1 (the task runs every 60 s)."""
        self.nodes[1].mockscheduler(61)

    def consolidations(self):
        n1 = self.nodes[1]
        seen, out = set(), []   # a send-to-self lists twice (send and receive)
        for t in n1.listtransactions("*", 1000):
            if t.get("comment", "").startswith("Automatic maintenance: merged") and t["txid"] not in seen:
                seen.add(t["txid"])
                out.append(t)
        return out

    def pay(self, address, amounts):
        n0 = self.nodes[0]
        for a in amounts:
            n0.sendtoaddress(address, a)
        n0.generatetoaddress(1, n0.getnewaddress())
        self.sync_all()

    def run_test(self):
        n0, n1 = self.nodes
        n0.generatetoaddress(120, n0.getnewaddress())
        self.sync_all()

        payout = n1.getnewaddress("pool payouts")
        other = n1.getnewaddress("other")
        self.pay(payout, [Decimal("0.5") + Decimal(i) / 100 for i in range(30)])  # 30 small, one address
        self.pay(other, [Decimal("0.7")] * 5)                                     # 5 small elsewhere
        self.pay(n1.getnewaddress(), [5])                                         # one large coin
        before = n1.getbalances()["mine"]["trusted"]

        self.log.info("turned off: nothing happens")
        self.tick()
        assert_equal(self.consolidations(), [])
        assert_equal(n1.getmempoolinfo()["size"], 0)

        self.log.info("turned on: the 15 smallest coins of the payout address merge into one")
        self.restart_node(1, ARGS)
        self.connect_nodes(0, 1)
        self.log.info("  but not while the wallet is behind the chain (tip over an hour old)")
        import time
        n1.setmocktime(int(time.time()) + 2 * 3600)
        self.tick()
        assert_equal(self.consolidations(), [])
        n1.setmocktime(0)
        self.tick()
        self.wait_until(lambda: len(self.consolidations()) == 1)
        c = self.consolidations()[0]
        tx = n1.gettransaction(c["txid"], True, True)["decoded"]
        assert_equal(len(tx["vin"]), 15)
        assert_equal(len(tx["vout"]), 1)
        spent = [n1.gettransaction(i["txid"], True, True)["decoded"]["vout"][i["vout"]] for i in tx["vin"]]
        assert all(o["scriptPubKey"]["addresses"] == [payout] for o in spent), "touched another address"
        assert_equal(sorted(Decimal(str(o["value"])) for o in spent),
                     [Decimal("0.5") + Decimal(i) / 100 for i in range(15)])           # the smallest 15
        dest = tx["vout"][0]["scriptPubKey"]["addresses"][0]
        assert_equal(dest, payout)   # back to the same address: no new key, nothing newly linked
        fee = -n1.gettransaction(c["txid"])["fee"]
        self.log.info("  merged 15 coins, fee %s" % fee)

        self.log.info("no second batch until the first confirms")
        self.tick()
        assert_equal(len(self.consolidations()), 1)
        self.sync_mempools()
        n0.generatetoaddress(1, n0.getnewaddress())
        self.sync_all()
        self.log.info("confirmed: 15 small coins left on the address, at the threshold of 20, so nothing more")
        self.tick()
        assert_equal(len(self.consolidations()), 1)

        self.log.info("more payouts push it over again; a locked wallet waits")
        n1.encryptwallet("pass")   # restarts nothing in 0.21; the wallet is locked afterwards
        self.pay(payout, [Decimal("0.6")] * 10)   # 25 small coins on the payout address
        self.tick()
        assert_equal(len(self.consolidations()), 1)
        n1.walletpassphrase("pass", 120)
        self.tick()
        self.wait_until(lambda: len(self.consolidations()) == 2)
        n1.walletlock()
        self.sync_mempools()
        n0.generatetoaddress(1, n0.getnewaddress())
        self.sync_all()

        self.log.info("-consolidateall merges across addresses")
        self.restart_node(1, ARGS + ["-consolidateall", "-consolidatemin=5"])
        self.connect_nodes(0, 1)
        n1.walletpassphrase("pass", 120)
        self.tick()
        self.wait_until(lambda: len(self.consolidations()) == 3)
        tx = n1.gettransaction(self.consolidations()[-1]["txid"], True, True)["decoded"]
        addrs = {n1.gettransaction(i["txid"], True, True)["decoded"]["vout"][i["vout"]]["scriptPubKey"]["addresses"][0] for i in tx["vin"]}
        assert other in addrs and payout in addrs, addrs

        self.log.info("nothing left the wallet but fees")
        self.sync_mempools()
        n0.generatetoaddress(1, n0.getnewaddress())
        self.sync_all()
        after = n1.getbalances()["mine"]["trusted"]
        fees = sum(-n1.gettransaction(t["txid"])["fee"] for t in self.consolidations())
        assert 0 < fees < Decimal("0.01"), fees
        assert_equal(after, before + Decimal("6.0") - fees)   # + the ten 0.6 payouts, - fees only


        self.log.info("at most one merge an hour (the default gap), also across a restart")
        import time
        gap_args = [x for x in ARGS if not x.startswith("-consolidategap")] + ["-consolidatemin=5"]
        self.restart_node(1, gap_args)   # the earlier merges were seconds ago
        self.connect_nodes(0, 1)
        n1.walletpassphrase("pass", 100000)
        self.pay(n1.getnewaddress("gap"), [Decimal("0.9")] * 40)   # a backlog: 40 small coins, 15 per merge
        count = len(self.consolidations())
        self.tick()
        assert_equal(len(self.consolidations()), count)   # inside the hour: nothing

        def hour_later(hours):
            t = int(time.time()) + hours * 3660
            for n in (n0, n1):
                n.setmocktime(t)
            n0.generatetoaddress(1, n0.getnewaddress())   # a fresh tip, or the wallet counts as behind
            self.sync_blocks()   # not mempools: transaction relay stands still under mocktime

        hour_later(1)
        self.tick()
        self.wait_until(lambda: len(self.consolidations()) == count + 1)
        n1.generatetoaddress(1, n0.getnewaddress())   # node1 confirms its own merge
        self.sync_blocks()
        assert_equal(n1.gettransaction(self.consolidations()[-1]["txid"])["confirmations"], 1)
        self.tick()
        assert_equal(len(self.consolidations()), count + 1)   # confirmed, backlog left, but the hour is not up
        self.restart_node(1, gap_args + ["-mocktime=%d" % (int(time.time()) + 3660 + 600)])
        self.connect_nodes(0, 1)
        n1.walletpassphrase("pass", 100000)
        self.tick()
        assert_equal(len(self.consolidations()), count + 1)   # a restart does not reset the gap
        hour_later(2)
        self.tick()
        self.wait_until(lambda: len(self.consolidations()) == count + 2)


if __name__ == '__main__':
    ConsolidateBobTest().main()
