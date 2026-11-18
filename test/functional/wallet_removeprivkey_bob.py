#!/usr/bin/env python3
# Copyright (c) 2026 The Dobbscoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""(BOB) #46, ported from 0.14's qa/rpc-tests/removeprivkey.py.

 1. signmessagewithprivkey signs on a node that does not hold the key; the
    signature verifies, equals signmessage's byte for byte (RFC 6979 nonces),
    and the key is not added to the wallet. A malformed key is refused.
 2. removeprivkey: the coins become unspendable here, dumpprivkey fails, the
    address's label goes with the key, and all of that survives a restart.
    Removing it twice fails.
 3. keepwatchonly=true: the balance stays visible as watch-only and the label
    stays. A change address (no address-book entry) is refused unless force.
    A key still in the keypool and the HD seed are refused.
 4. importprivkey brings a removed key (and its coins) back.
 5. Encrypted wallet: refused while locked, works once unlocked, persists.
"""

import os

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error

MSG = "x_static"
B58 = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"


class RemovePrivKeyTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()
        # removeprivkey is for legacy wallets: run with --legacy-wallet

    def fund(self, addr, amount):
        self.nodes[0].sendtoaddress(addr, amount)
        self.nodes[0].generate(1)
        self.sync_all()

    def ismine(self, node, addr):
        return node.getaddressinfo(addr)["ismine"]

    def assert_secret_gone(self, node, wif):
        raw = 0
        for c in wif:
            raw = raw * 58 + B58.index(c)
        secret = raw.to_bytes((raw.bit_length() + 7) // 8, "big")[1:33]
        wallets = os.path.join(node.datadir, self.chain, "wallets")
        for root, _, files in os.walk(wallets):
            for f in files:
                with open(os.path.join(root, f), "rb") as fh:
                    assert secret not in fh.read(), "secret bytes still in " + os.path.join(root, f)

    def restart1(self):
        self.restart_node(1)
        self.connect_nodes(0, 1)

    def run_test(self):
        n0, n1 = self.nodes
        n0.generate(101)
        self.sync_all()

        self.log.info("signmessagewithprivkey")
        addr = n0.getnewaddress("", "legacy")
        wif = n0.dumpprivkey(addr)
        sig = n1.signmessagewithprivkey(wif, MSG)
        assert n1.verifymessage(addr, sig, MSG)
        assert_equal(sig, n0.signmessage(addr, MSG))
        assert not self.ismine(n1, addr)  # nothing was imported
        assert_raises_rpc_error(-5, None, n1.signmessagewithprivkey, "not-a-key", MSG)

        self.log.info("removeprivkey")
        a = n1.getnewaddress("alpha", "legacy")
        self.fund(a, 10)
        assert_equal(n1.getbalance(), 10)
        wif_a = n1.dumpprivkey(a)
        n1.removeprivkey(a)
        assert_raises_rpc_error(-4, None, n1.dumpprivkey, a)
        assert "alpha" not in n1.listlabels()  # its address-book entry went too
        assert not self.ismine(n1, a)
        assert_equal(n1.getbalance(), 0)
        self.restart1()
        n1 = self.nodes[1]
        assert not self.ismine(n1, a)
        assert_equal(n1.getbalance(), 0)
        assert "alpha" not in n1.listlabels()
        self.assert_secret_gone(n1, wif_a)  # wallet file and its log, once the node has shut down cleanly
        assert_raises_rpc_error(-4, "not in the wallet", n1.removeprivkey, a)
        assert_raises_rpc_error(-5, "Invalid Dobbscoin address", n1.removeprivkey, "notanaddress")

        self.log.info("removeprivkey with keepwatchonly")
        b = n1.getnewaddress("bravo", "legacy")
        self.fund(b, 5)
        n1.removeprivkey(b, True)
        info = n1.getaddressinfo(b)
        assert not info["ismine"] and info["iswatchonly"]
        assert "bravo" in n1.listlabels()  # a watched address keeps its entry
        assert_equal(n1.getbalance(), 0)
        assert_equal(n1.getbalance("*", 1, True), 5)
        self.restart1()
        n1 = self.nodes[1]
        assert n1.getaddressinfo(b)["iswatchonly"]
        assert_equal(n1.getbalance("*", 1, True), 5)

        self.log.info("refuses a change address without force")
        ch = n1.getrawchangeaddress("legacy")
        assert_raises_rpc_error(-4, "change address", n1.removeprivkey, ch)
        assert self.ismine(n1, ch)
        n1.removeprivkey(ch, False, True)
        assert not self.ismine(n1, ch)

        self.log.info("refuses the HD seed and keys still in the keypool, even with force")
        dump = os.path.join(self.options.tmpdir, "n1.dump")
        n1.dumpwallet(dump)
        seed_addr = reserve_addr = None
        for line in open(dump, encoding="utf8"):
            if "addr=" not in line:
                continue
            first = line.split("addr=")[1].split()[0].split(",")[0]
            if "hdseed=1" in line:
                seed_addr = first
            elif "reserve=1" in line and reserve_addr is None:
                reserve_addr = first
        assert seed_addr and reserve_addr
        assert_raises_rpc_error(-4, "HD seed", n1.removeprivkey, seed_addr, False, True)
        assert_raises_rpc_error(-4, "keypool", n1.removeprivkey, reserve_addr, False, True)
        assert self.ismine(n1, seed_addr) and self.ismine(n1, reserve_addr)

        self.log.info("importprivkey brings it back")
        n1.importprivkey(wif_a, "alpha")
        assert self.ismine(n1, a)
        assert_equal(n1.getbalance(), 10)
        n1.removeprivkey(a)
        assert_equal(n1.getbalance(), 0)

        self.log.info("removeprivkey on an encrypted wallet")
        n1.encryptwallet("test passphrase")
        c = n1.getnewaddress("charlie", "legacy")
        self.fund(c, 3)
        assert_raises_rpc_error(-13, None, n1.removeprivkey, c)  # locked
        n1.walletpassphrase("test passphrase", 60)
        n1.removeprivkey(c)
        assert not self.ismine(n1, c)
        n1.walletlock()
        self.restart1()
        n1 = self.nodes[1]
        assert not self.ismine(n1, c)
        n1.walletpassphrase("test passphrase", 60)
        assert_raises_rpc_error(-4, None, n1.dumpprivkey, c)

        self.log.info("descriptor wallets refuse it")
        n1.createwallet(wallet_name="desc", descriptors=True)
        w = n1.get_wallet_rpc("desc")
        d = w.getnewaddress("", "legacy")
        assert_raises_rpc_error(-4, None, w.removeprivkey, d)


if __name__ == '__main__':
    RemovePrivKeyTest().main()
