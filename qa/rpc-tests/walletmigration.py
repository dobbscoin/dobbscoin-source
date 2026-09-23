#!/usr/bin/env python3
# Copyright (c) 2026 The Dobbscoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
The wallet moved from Berkeley DB to SQLite (issue #44). This test checks the
move from the user's side:

 1. A Berkeley DB wallet.dat written by v0.13.8 is migrated at startup: the
    original is kept byte for byte as wallet.dat.bdb-<unixtime>, wallet.dat is
    now SQLite, every key and label is still there, and the migrated keys can
    receive and spend.
 2. An encrypted v0.13.8 wallet migrates without its passphrase, refuses a
    wrong one, unlocks with the original, and spends.
 3. A damaged Berkeley DB wallet (truncated; not cleanly closed) stops the node
    with an error and is left untouched, with nothing written beside it.
 4. The encrypt-wallet round trip on an SQLite wallet: after encryptwallet the
    file holds ckey and mkey records, no key records, and none of the private
    keys' bytes anywhere; it unlocks after a restart.
 5. backupwallet writes an SQLite copy holding the wallet's records.

The fixtures in qa/rpc-tests/data/ were written by the unmodified v0.13.8
build (tag v0.13.8, BDB 4.8.30) on regtest with -keypool=2:
  plain:     getnewaddress alice; getnewaddress bob;
             importaddress mfWxJ45yp2SFn7UciZyNpvDKrzbhyfKrY8 watched false
  encrypted: getnewaddress carol; encryptwallet "fixture passphrase"
They are regtest throwaways; their keys must never hold real coins.
"""

import hashlib
import os
import shutil
import sqlite3
import subprocess
import time

from test_framework import DobbscoinTestFramework
from util import *

DATA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data")
PLAIN = os.path.join(DATA, "wallet-v0.13.8-bdb-plain.dat")
ENCRYPTED = os.path.join(DATA, "wallet-v0.13.8-bdb-encrypted.dat")
PASSPHRASE = "fixture passphrase"

# What v0.13.8's dumpwallet listed for each fixture: address -> label, or None
# for keypool / change keys.
PLAIN_KEYS = {
    "mvnTgYG1iTtdF5SXoTDXExBCM8bamo2EfC": "bob",
    "miC1DeQ1Px7wr8wZ8wzE7vyvnmXbBtGYyN": "alice",
    "mvJnKqGXTgrpbzmq9R52vWBi7xcbNs4fYV": "",
    "mkPwceVFmEkTGX3gttV1THcwb6w5QeWAVZ": None,
    "mn1SjZmGG97uzcTzCzWATStgD58Q8QY7Nd": None,
}
PLAIN_WATCHED = "mfWxJ45yp2SFn7UciZyNpvDKrzbhyfKrY8"
ENCRYPTED_KEYS = {
    "mpLLLF5yPiQ12BAWb4ydCRxP8VTYZ3so7G": None,
    "mma1mbc3cAjYXyesj1LAdxmgBWi4kDSoLz": None,
    "n2hJ6Ywf5BHitpHjRwFbzyL3a31vFkeZLS": None,
    "msgG4JnTDXgea3ghsRBMJxQgcvjJcJkG69": "carol",
    "mywUS2YqKjm3aWZFwxfM53EpXMtgFCfsMF": None,
    "my8mxy2XPKkRU89BK4tRF4aPsVxY74Bm5S": "",
    "myPMd52ZjiesCy5UQ9zqAvpSnyb7KfaLw7": None,
}

B58 = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"


def b58decode_check(s):
    n = 0
    for c in s:
        n = n * 58 + B58.index(c)
    raw = n.to_bytes((n.bit_length() + 7) // 8, "big")
    raw = b"\0" * (len(s) - len(s.lstrip("1"))) + raw
    payload, check = raw[:-4], raw[-4:]
    assert hashlib.sha256(hashlib.sha256(payload).digest()).digest()[:4] == check
    return payload


def sha256_file(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def dump_keys(node, path):
    """address -> label (None for reserve/change keys) and address -> WIF, from dumpwallet."""
    node.dumpwallet(path)
    labels, wifs = {}, {}
    for line in open(path):
        if not line.strip() or line.startswith("#"):
            continue
        fields = line.split()
        wif, meta = fields[0], fields[2]
        addr = [f for f in fields if f.startswith("addr=")][0][5:]
        labels[addr] = meta[6:] if meta.startswith("label=") else None
        wifs[addr] = wif
    return labels, wifs


def wallet_records(path):
    db = sqlite3.connect("file:%s?mode=ro" % path, uri=True)
    rows = db.execute("SELECT key, value FROM main ORDER BY key").fetchall()
    app_id = db.execute("PRAGMA application_id").fetchone()[0]
    check = db.execute("PRAGMA quick_check").fetchone()[0]
    db.close()
    return rows, app_id, check


def record_type(key):
    n = key[0]
    return key[1:1 + n].decode("latin-1")


class WalletMigrationTest(DobbscoinTestFramework):

    def setup_chain(self):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain(self.options.tmpdir)

    def setup_network(self, split=False):
        self.nodes = start_nodes(3, self.options.tmpdir)
        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 0, 2)
        self.is_network_split = False
        self.sync_all()

    def regtest_dir(self, i):
        return os.path.join(self.options.tmpdir, "node%d" % i, "regtest")

    def restart_with_wallet(self, i, source, extra=None, running=True):
        if running:
            stop_node(self.nodes[i], i)
        rd = self.regtest_dir(i)
        for f in os.listdir(rd):
            if f.startswith("wallet.dat"):
                os.remove(os.path.join(rd, f))
        shutil.copyfile(source, os.path.join(rd, "wallet.dat"))
        self.nodes[i] = start_node(i, self.options.tmpdir, extra)
        connect_nodes_bi(self.nodes, 0, i)

    def restart(self, i, extra=None):
        stop_node(self.nodes[i], i)
        self.nodes[i] = start_node(i, self.options.tmpdir, extra)
        connect_nodes_bi(self.nodes, 0, i)

    def check_migrated(self, i, source):
        rd = self.regtest_dir(i)
        files = sorted(f for f in os.listdir(rd) if f.startswith("wallet.dat"))
        assert_equal(len(files), 2)
        assert_equal(files[0], "wallet.dat")
        assert files[1].startswith("wallet.dat.bdb-"), files
        # The original, untouched, beside the new file.
        assert_equal(sha256_file(os.path.join(rd, files[1])), sha256_file(source))
        with open(os.path.join(rd, "wallet.dat"), "rb") as f:
            assert_equal(f.read(16), b"SQLite format 3\x00")
        log = open(os.path.join(rd, "debug.log")).read()
        assert "verified byte for byte" in log

    def fund_and_spend(self, i, to_addr):
        n0, ni = self.nodes[0], self.nodes[i]
        before = ni.getbalance()
        n0.sendtoaddress(to_addr, 10)
        self.sync_all()
        n0.setgenerate(True, 1)
        self.sync_all()
        assert_equal(ni.getbalance(), before + 10)
        txid = ni.sendtoaddress(n0.getnewaddress(), 4)
        self.sync_all()
        n0.setgenerate(True, 1)
        self.sync_all()
        assert_equal(ni.gettransaction(txid)["confirmations"], 1)
        after = ni.getbalance()
        assert before + 6 - Decimal("0.01") < after < before + 6, after
        return txid

    def refuse_to_start(self, i, damaged, why):
        """Start (stopped) node i on a damaged wallet; it must exit with an error and leave the file alone."""
        rd = self.regtest_dir(i)
        for f in os.listdir(rd):
            if f.startswith("wallet.dat"):
                os.remove(os.path.join(rd, f))
        path = os.path.join(rd, "wallet.dat")
        with open(path, "wb") as f:
            f.write(damaged)
        datadir = os.path.join(self.options.tmpdir, "node%d" % i)
        proc = subprocess.run([os.getenv("DOBBSCOIND", "dobbscoind"), "-datadir=" + datadir, "-keypool=1",
                               "-discover=0"], capture_output=True, text=True, timeout=120)
        assert proc.returncode != 0, "node started on a damaged wallet"
        assert why in proc.stderr + proc.stdout, proc.stderr + proc.stdout
        with open(path, "rb") as f:
            assert f.read() == damaged, "the damaged wallet was modified"
        assert_equal(sorted(f for f in os.listdir(rd) if f.startswith("wallet.dat")), ["wallet.dat"])

    def run_test(self):
        # --- 1. plain v0.13.8 wallet -------------------------------------------------
        print("Migrating a plain v0.13.8 wallet")
        # -keypool=0: no fresh keys, so the key set must be exactly the fixture's
        self.restart_with_wallet(1, PLAIN, ["-keypool=0"])
        self.check_migrated(1, PLAIN)
        labels, _ = dump_keys(self.nodes[1], os.path.join(self.options.tmpdir, "plain.dump"))
        assert_equal(labels, PLAIN_KEYS)
        assert_equal(self.nodes[1].getaccount("miC1DeQ1Px7wr8wZ8wzE7vyvnmXbBtGYyN"), "alice")
        assert_equal(self.nodes[1].validateaddress(PLAIN_WATCHED)["iswatchonly"], True)
        self.restart(1)
        self.fund_and_spend(1, "miC1DeQ1Px7wr8wZ8wzE7vyvnmXbBtGYyN")
        self.nodes[0].sendtoaddress(PLAIN_WATCHED, 3)
        self.sync_all()
        self.nodes[0].setgenerate(True, 1)
        self.sync_all()
        assert_equal(self.nodes[1].getbalance("*", 1, True) - self.nodes[1].getbalance("*", 1), 3)
        self.restart(1)  # a second start is an ordinary SQLite load, no second migration
        assert_equal(len([f for f in os.listdir(self.regtest_dir(1)) if f.startswith("wallet.dat.bdb-")]), 1)

        # --- 2. encrypted v0.13.8 wallet ---------------------------------------------
        print("Migrating an encrypted v0.13.8 wallet")
        self.restart_with_wallet(1, ENCRYPTED, ["-keypool=0"])
        self.check_migrated(1, ENCRYPTED)
        n1 = self.nodes[1]
        assert_raises(JSONRPCException, n1.walletpassphrase, "not the passphrase", 10)
        n1.walletpassphrase(PASSPHRASE, 60)
        assert n1.getinfo()["unlocked_until"] > 0
        labels, _ = dump_keys(n1, os.path.join(self.options.tmpdir, "encrypted.dump"))
        assert_equal(labels, ENCRYPTED_KEYS)
        n1.walletlock()
        self.restart(1)
        self.nodes[1].walletpassphrase(PASSPHRASE, 60)
        self.fund_and_spend(1, "msgG4JnTDXgea3ghsRBMJxQgcvjJcJkG69")

        # --- 3. damaged Berkeley DB wallets ------------------------------------------
        print("Refusing damaged Berkeley DB wallets")
        plain = open(PLAIN, "rb").read()
        stop_node(self.nodes[1], 1)
        self.refuse_to_start(1, plain[:-4096], "truncated")
        unflushed = bytearray(plain)
        unflushed[4096 + 4] = 2  # page 1 LSN offset 1 -> 2: the log still holds data
        self.refuse_to_start(1, bytes(unflushed), "not closed cleanly")
        self.restart_with_wallet(1, PLAIN, running=False)  # and the intact file still migrates
        self.check_migrated(1, PLAIN)

        # --- 4. encrypt-wallet round trip on SQLite ---------------------------------
        print("Encrypting an SQLite wallet")
        n2 = self.nodes[2]
        _, wifs = dump_keys(n2, os.path.join(self.options.tmpdir, "node2-before.dump"))
        secrets = [b58decode_check(w)[1:33] for w in wifs.values()]
        assert len(secrets) > 0
        n2.encryptwallet("round trip passphrase")
        dobbscoind_processes[2].wait()  # encryptwallet stops the node
        del dobbscoind_processes[2]
        rd = self.regtest_dir(2)
        assert_equal(sorted(f for f in os.listdir(rd) if f.startswith("wallet.dat")), ["wallet.dat"])  # no journal left
        rows, app_id, check = wallet_records(os.path.join(rd, "wallet.dat"))
        assert_equal(check, "ok")
        assert_equal(app_id, 0x424f4257)
        types = [record_type(k) for k, v in rows]
        assert types.count("ckey") > 0, "no ckey records"
        assert types.count("mkey") == 1, "no mkey record"
        assert types.count("key") == 0, "cleartext key records left"
        raw = open(os.path.join(rd, "wallet.dat"), "rb").read()
        leaked = [s.hex() for s in secrets if s in raw]
        assert_equal(leaked, [])
        self.nodes[2] = start_node(2, self.options.tmpdir)
        assert_raises(JSONRPCException, self.nodes[2].walletpassphrase, "wrong", 10)
        self.nodes[2].walletpassphrase("round trip passphrase", 60)
        assert self.nodes[2].getinfo()["unlocked_until"] > 0

        # --- 5. backupwallet ---------------------------------------------------------
        print("Backing up an SQLite wallet")
        backup = os.path.join(self.options.tmpdir, "backup.dat")
        self.nodes[0].backupwallet(backup)
        rows, app_id, check = wallet_records(backup)
        assert_equal(check, "ok")
        assert_equal(app_id, 0x424f4257)
        live_addrs = set(dump_keys(self.nodes[0], os.path.join(self.options.tmpdir, "node0.dump"))[0])
        stop_node(self.nodes[0], 0)
        live_rows, _, _ = wallet_records(os.path.join(self.regtest_dir(0), "wallet.dat"))
        keyish = lambda rs: sorted((k, v) for k, v in rs if record_type(k) in ("key", "ckey", "name", "pool", "keymeta"))
        assert_equal(keyish(rows), keyish(live_rows))
        # and the copy works as a wallet
        shutil.copyfile(backup, os.path.join(self.regtest_dir(3), "wallet.dat"))
        node3 = start_node(3, self.options.tmpdir, ["-keypool=0"])
        assert_equal(set(dump_keys(node3, os.path.join(self.options.tmpdir, "node3.dump"))[0]), live_addrs)
        stop_node(node3, 3)
        self.nodes[0] = start_node(0, self.options.tmpdir)


if __name__ == '__main__':
    WalletMigrationTest().main()
