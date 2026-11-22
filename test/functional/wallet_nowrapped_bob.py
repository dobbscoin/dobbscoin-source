#!/usr/bin/env python3
# Copyright (c) 2026 The Dobbscoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""(BOB) the wallet never creates P2SH-wrapped SegWit addresses.

To a 0.14 node a wrapped SegWit output is an ordinary P2SH output, and spending it
with no witness and no signature is a standard transaction there: stock 0.14 relays
and mines it. Native bech32 outputs are non-standard to 0.14. So after activation the
wallet offers bech32 and legacy only, for receiving, change and multisig.
-allowwrappedsegwit (tests only) restores the upstream behaviour.
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error

ACTIVATION = 160
MSG = "Wrapped SegWit (p2sh-segwit) addresses are not offered"


class NoWrappedSegwitBobTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.base = ["-segwitheight=%d" % ACTIVATION, "-csvheight=%d" % ACTIVATION]
        self.extra_args = [self.base]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def kind(self, node, addr):
        info = node.getaddressinfo(addr)
        if info["iswitness"]:
            return "bech32"
        return "p2sh" if info["isscript"] else "legacy"

    def change_kind(self, node, txid):
        tx = node.gettransaction(txid, True, True)["decoded"]
        kinds = []
        for o in tx["vout"]:
            addr = o["scriptPubKey"]["addresses"][0]
            if node.getaddressinfo(addr).get("ischange"):
                kinds.append(self.kind(node, addr))
        assert_equal(len(kinds), 1)
        return kinds[0]

    def run_test(self):
        n = self.nodes[0]
        n.generatetoaddress(ACTIVATION + 5, n.getnewaddress("", "legacy"))

        self.log.info("after activation: bech32 and legacy are offered, wrapped is refused")
        assert_equal(self.kind(n, n.getnewaddress("", "bech32")), "bech32")
        assert_equal(self.kind(n, n.getnewaddress("", "legacy")), "legacy")
        assert_equal(self.kind(n, n.getnewaddress()), "legacy")
        assert_raises_rpc_error(-12, MSG, n.getnewaddress, "", "p2sh-segwit")
        assert_raises_rpc_error(-12, MSG, n.getrawchangeaddress, "p2sh-segwit")
        keys = [n.getaddressinfo(n.getnewaddress("", "legacy"))["pubkey"] for _ in range(2)]
        assert_raises_rpc_error(-8, MSG, n.addmultisigaddress, 2, keys, "", "p2sh-segwit")
        assert_equal(self.kind(n, n.addmultisigaddress(2, keys, "", "bech32")["address"]), "bech32")

        self.log.info("a wrapped change type falls back to native SegWit change")
        txid = n.sendtoaddress(n.getnewaddress("", "legacy"), 1)
        assert_equal(self.change_kind(n, txid), "legacy")
        self.restart_node(0, self.base + ["-changetype=p2sh-segwit"])
        txid = n.sendtoaddress(n.getnewaddress("", "legacy"), 1)
        assert_equal(self.change_kind(n, txid), "bech32")
        self.restart_node(0, self.base + ["-addresstype=p2sh-segwit"])
        assert_raises_rpc_error(-12, MSG, n.getnewaddress)
        txid = n.sendtoaddress(n.getnewaddress("", "legacy"), 1)
        assert_equal(self.change_kind(n, txid), "bech32")
        n.generatetoaddress(1, n.getnewaddress("", "legacy"))

        self.log.info("-allowwrappedsegwit (tests only) restores wrapped addresses")
        self.restart_node(0, self.base + ["-allowwrappedsegwit=1"])
        assert_equal(self.kind(n, n.getnewaddress("", "p2sh-segwit")), "p2sh")


if __name__ == '__main__':
    NoWrappedSegwitBobTest().main()
