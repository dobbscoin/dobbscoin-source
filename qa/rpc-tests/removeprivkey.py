#!/usr/bin/env python3
# Copyright (c) 2026 The Dobbscoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""
Issue #46: a key can be used to sign without importing it, and a key that was
imported can be taken back out.

 1. signmessagewithprivkey signs on a node that does not hold the key; the
    signature verifies, equals signmessage's byte for byte (RFC 6979 nonces),
    and the key is not added to the wallet. A malformed key is refused.
 2. removeprivkey on a plain wallet: the coins become unspendable here,
    dumpprivkey fails, the key's records and its secret bytes are gone from
    wallet.dat, and all of that survives a restart. Removing it twice fails.
 3. keepwatchonly=true: the balance stays visible as watch-only. A change
    address (no address-book entry) is refused unless force=true.
 4. importprivkey brings a removed key (and its coins) back.
 5. Encrypted wallet: refused while locked, works once unlocked, persists.
"""

import hashlib
import os
import sqlite3

from test_framework import DobbscoinTestFramework
from dobbscoinrpc.authproxy import JSONRPCException
from util import *

B58 = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"
MSG = "x_static"


def b58decode_check(s):
    n = 0
    for c in s:
        n = n * 58 + B58.index(c)
    raw = n.to_bytes((n.bit_length() + 7) // 8, "big")
    raw = b"\0" * (len(s) - len(s.lstrip("1"))) + raw
    payload, check = raw[:-4], raw[-4:]
    assert hashlib.sha256(hashlib.sha256(payload).digest()).digest()[:4] == check
    return payload


def rpc_error_code(fun, *args):
    try:
        fun(*args)
    except JSONRPCException as e:
        return e.error["code"]
    raise AssertionError("expected an RPC error from %s%r" % (fun, args))


class RemovePrivKeyTest(DobbscoinTestFramework):

    def setup_chain(self):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 2)

    def setup_network(self, split=False):
        self.nodes = start_nodes(2, self.options.tmpdir)
        connect_nodes_bi(self.nodes, 0, 1)
        self.is_network_split = False
        self.sync_all()

    def wallet_file(self, i):
        return os.path.join(self.options.tmpdir, "node%d" % i, "regtest", "wallet.dat")

    def restart(self, i):
        stop_node(self.nodes[i], i)
        self.nodes[i] = start_node(i, self.options.tmpdir)
        connect_nodes_bi(self.nodes, 0, 1)

    def fund(self, addr, amount):
        self.nodes[0].sendtoaddress(addr, amount)
        self.nodes[0].setgenerate(True, 1)
        self.sync_all()

    def assert_key_gone_from_file(self, i, secret, pubkey_hex):
        path = self.wallet_file(i)
        assert secret not in open(path, "rb").read(), "secret bytes still in wallet.dat"
        pub = bytes.fromhex(pubkey_hex)
        db = sqlite3.connect("file:%s?mode=ro" % path, uri=True)
        for (k,) in db.execute("SELECT key FROM main"):
            for rec in (b"\x03key", b"\x04wkey", b"\x04ckey", b"\x07keymeta"):
                assert not (k.startswith(rec) and pub in k), "a %r record for the key is still there" % rec
        db.close()

    def run_test(self):
        n0, n1 = self.nodes
        n0.setgenerate(True, 101)
        self.sync_all()

        # --- 1. signmessagewithprivkey -------------------------------------------------
        print("signmessagewithprivkey")
        addr = n0.getnewaddress()
        wif = n0.dumpprivkey(addr)
        sig = n1.signmessagewithprivkey(wif, MSG)
        assert n1.verifymessage(addr, sig, MSG)
        assert_equal(sig, n0.signmessage(addr, MSG))
        assert not n1.validateaddress(addr)["ismine"]        # nothing was imported
        assert_equal(rpc_error_code(n1.signmessagewithprivkey, "not-a-key", MSG), -5)

        # --- 2. removeprivkey, plain wallet ---------------------------------------------
        print("removeprivkey on a plain wallet")
        a = n1.getnewaddress()
        self.fund(a, 10)
        assert_equal(float(n1.getbalance()), 10.0)
        wif_a = n1.dumpprivkey(a)
        secret_a = b58decode_check(wif_a)[1:33]
        pub_a = n1.validateaddress(a)["pubkey"]
        n1.removeprivkey(a)
        assert_equal(rpc_error_code(n1.dumpprivkey, a), -4)
        assert not n1.validateaddress(a)["ismine"]
        assert_equal(float(n1.getbalance()), 0.0)
        self.restart(1)
        n1 = self.nodes[1]
        assert not n1.validateaddress(a)["ismine"]
        assert_equal(float(n1.getbalance()), 0.0)
        self.assert_key_gone_from_file(1, secret_a, pub_a)
        assert_equal(rpc_error_code(n1.removeprivkey, a), -4)   # not in the wallet any more
        assert_equal(rpc_error_code(n1.removeprivkey, "notanaddress"), -5)

        # --- 3. keepwatchonly -----------------------------------------------------------
        print("removeprivkey with keepwatchonly")
        b = n1.getnewaddress()
        self.fund(b, 5)
        n1.removeprivkey(b, True)
        v = n1.validateaddress(b)
        assert not v["ismine"] and v["iswatchonly"]
        assert_equal(float(n1.getbalance()), 0.0)
        assert_equal(float(n1.getbalance("*", 1, True)), 5.0)

        # --- 3b. a change address needs force ------------------------------------------
        print("removeprivkey refuses a change address without force")
        ch = n1.getrawchangeaddress()
        assert_equal(rpc_error_code(n1.removeprivkey, ch), -4)
        assert n1.validateaddress(ch)["ismine"]
        n1.removeprivkey(ch, False, True)
        assert not n1.validateaddress(ch)["ismine"]

        # --- 4. a removed key can be imported again -------------------------------------
        print("importprivkey brings it back")
        n1.importprivkey(wif_a)
        assert n1.validateaddress(a)["ismine"]
        assert_equal(float(n1.getbalance()), 10.0)
        n1.removeprivkey(a)
        assert_equal(float(n1.getbalance()), 0.0)

        # --- 5. encrypted wallet ----------------------------------------------------------
        print("removeprivkey on an encrypted wallet")
        n1.encryptwallet("test passphrase")
        dobbscoind_processes[1].wait()      # encryptwallet stops the node
        del dobbscoind_processes[1]
        self.nodes[1] = start_node(1, self.options.tmpdir)
        connect_nodes_bi(self.nodes, 0, 1)
        n1 = self.nodes[1]
        c = n1.getnewaddress()
        self.fund(c, 3)
        pub_c = n1.validateaddress(c)["pubkey"]
        assert_equal(rpc_error_code(n1.removeprivkey, c), -13)   # locked
        n1.walletpassphrase("test passphrase", 60)
        wif_c = n1.dumpprivkey(c)
        n1.removeprivkey(c)
        assert not n1.validateaddress(c)["ismine"]
        n1.walletlock()
        self.restart(1)
        n1 = self.nodes[1]
        assert not n1.validateaddress(c)["ismine"]
        self.assert_key_gone_from_file(1, b58decode_check(wif_c)[1:33], pub_c)
        n1.walletpassphrase("test passphrase", 60)
        assert_equal(rpc_error_code(n1.dumpprivkey, c), -4)


if __name__ == '__main__':
    RemovePrivKeyTest().main()
