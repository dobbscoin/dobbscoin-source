#!/usr/bin/env python3
# Copyright (c) 2026 The Dobbscoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""(BOB) SegWit/CSV activation rehearsal on regtest (EndTimes only).

Activation by height (-segwitheight / -csvheight), as (BOB) would do it: version
signaling cannot work once AuxPoW stamps the chain ID into every block version.
Before the height: the wallet refuses bech32, and a BIP68 relative timelock is not
enforced. From the height: bech32 receive and witness spend confirm, blocks carry a
witness commitment, BIP68 is enforced (a relative-locked spend is refused until its
input is old enough), and legacy sends keep working. Everything survives a restart.
"""

from decimal import Decimal

from test_framework.messages import COIN, CTransaction, FromHex, ToHex
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error

ACTIVATION = 300  # regtest: AuxPoW at 10, CLTV at 150


class SegwitCsvBobTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        args = ["-segwitheight=%d" % ACTIVATION, "-csvheight=%d" % ACTIVATION, "-acceptnonstdtxn=0"]
        self.extra_args = [args, args]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def mine_to(self, node, height):
        need = height - node.getblockcount()
        if need > 0:
            node.generatetoaddress(need, node.getnewaddress("", "legacy"))
        self.sync_all()

    def csv_spend(self, node, txid, vout, value, rel_blocks):
        """A version-2 spend of txid:vout with a BIP68 relative lock of rel_blocks."""
        dest = node.getnewaddress("", "legacy")
        raw = node.createrawtransaction([{"txid": txid, "vout": vout, "sequence": rel_blocks}],
                                        {dest: float(Decimal(value) - Decimal("0.01"))})
        tx = FromHex(CTransaction(), raw)
        tx.nVersion = 2
        return node.signrawtransactionwithwallet(ToHex(tx))["hex"]

    def fund_legacy(self, node, amount):
        addr = node.getnewaddress("", "legacy")
        txid = node.sendtoaddress(addr, amount)
        node.generatetoaddress(1, node.getnewaddress("", "legacy"))
        self.sync_all()
        vout = [i for i, o in enumerate(node.gettransaction(txid, True, True)["decoded"]["vout"])
                if o["scriptPubKey"].get("addresses", [None])[0] == addr][0]
        return txid, vout

    def run_test(self):
        n0, n1 = self.nodes
        self.mine_to(n0, 120)  # mature coinbases

        self.log.info("before activation: no bech32, no BIP68")
        assert_raises_rpc_error(-12, "until SegWit is activated", n0.getnewaddress, "", "bech32")
        txid, vout = self.fund_legacy(n0, 5)
        raw = self.csv_spend(n0, txid, vout, 5, 50)  # input has 1 confirmation, lock says 50
        # Mempool policy enforces BIP68 on v2 transactions regardless; the consensus
        # rule is what activates. Before the height a block may carry this spend.
        assert_raises_rpc_error(-26, "non-BIP68-final", n0.sendrawtransaction, raw)
        n0.generateblock(n0.getnewaddress("", "legacy"), [raw])
        self.sync_all()

        self.log.info("mine to the activation height %d" % ACTIVATION)
        self.mine_to(n0, ACTIVATION)

        self.log.info("after activation: bech32 receive and witness spend confirm")
        addr = n1.getnewaddress("", "bech32")
        assert addr.startswith("dcrt1") or addr.startswith("bob1"), addr
        txid = n0.sendtoaddress(addr, 7)
        n0.generatetoaddress(1, n0.getnewaddress("", "legacy"))
        self.sync_all()
        assert_equal(n1.gettransaction(txid)["confirmations"], 1)
        back = n1.sendtoaddress(n0.getnewaddress("", "legacy"), 3)  # spends the witness output
        tx = n1.getrawtransaction(back, True)
        assert any(i.get("txinwitness") for i in tx["vin"]), "the spend carries no witness"
        self.sync_mempools()  # the witness spend must relay to n0 (a timeout here = rejected)
        assert back in n0.getrawmempool()
        blk = n0.generatetoaddress(1, n0.getnewaddress("", "legacy"))[0]
        self.sync_all()
        assert_equal(n1.gettransaction(back)["confirmations"], 1)
        cb = n0.getblock(blk, 2)["tx"][0]
        assert any(o["scriptPubKey"]["hex"].startswith("6a24aa21a9ed") for o in cb["vout"]), "no witness commitment"

        self.log.info("after activation: BIP68 relative timelock enforced")
        txid, vout = self.fund_legacy(n0, 5)
        raw = self.csv_spend(n0, txid, vout, 5, 10)
        assert_raises_rpc_error(-26, "non-BIP68-final", n0.sendrawtransaction, raw)
        # ...and now consensus refuses a block that carries it too
        assert_raises_rpc_error(-25, "bad-txns-nonfinal", n0.generateblock, n0.getnewaddress("", "legacy"), [raw])
        n0.generatetoaddress(9, n0.getnewaddress("", "legacy"))
        self.sync_all()
        n0.sendrawtransaction(raw)  # input now 10 blocks deep
        n0.generatetoaddress(1, n0.getnewaddress("", "legacy"))
        self.sync_all()

        self.log.info("legacy sends still work")
        t = n0.sendtoaddress(n1.getnewaddress("", "legacy"), 1)
        n0.generatetoaddress(1, n0.getnewaddress("", "legacy"))
        self.sync_all()
        assert_equal(n1.gettransaction(t)["confirmations"], 1)

        self.log.info("both nodes agree after a restart")
        height = n0.getblockcount()
        self.restart_node(0, self.extra_args[0])
        self.restart_node(1, self.extra_args[1])
        self.connect_nodes(0, 1)
        assert_equal(self.nodes[0].getbestblockhash(), self.nodes[1].getbestblockhash())
        assert_equal(self.nodes[0].getblockcount(), height)
        assert self.nodes[1].getbalances()["mine"]["trusted"] > 0


if __name__ == '__main__':
    SegwitCsvBobTest().main()
