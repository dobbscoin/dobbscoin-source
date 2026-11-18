#!/usr/bin/env python3
# Copyright (c) 2026 The Dobbscoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""(BOB) the in-wallet stratum pool client (setstratum / getstratuminfo), from 0.14.

A stand-in pool in this script speaks the Miningcore dialect: subscribe ->
extranonce1 + extranonce2_size 4, authorize, set_difficulty, notify. Every
share the node submits is rebuilt here from scratch (coinbase = coinb1 |
extranonce1 | extranonce2 | coinb2, merkle fold, 80-byte header) and checked
with Python's own hashlib.scrypt against the share bound, so the client's share
construction is verified independently of the C++ code. Then a bad payout
address is refused, and setstratum false stops the client.
"""

import hashlib
import json
import os
import socket
import struct
import threading
import time

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error

EN1 = "f000000f"
DIFF = 1.0 / 256  # bound = DIFF1 * 65536 / DIFF ~ 2^248: about 1 hash in 256 qualifies
DIFF1 = 65535 * 2**208
MULT = 65536


def dsha(b):
    return hashlib.sha256(hashlib.sha256(b).digest()).digest()


class FakePool:
    def __init__(self):
        self.srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.srv.bind(("127.0.0.1", 0))
        self.srv.listen(4)
        self.port = self.srv.getsockname()[1]
        self.users = []
        self.good = 0
        self.bad = 0
        self.control_pass = 0  # good shares that still pass with the nonce changed
        self.lock = threading.Lock()
        self.stop = False
        # the job: random coinbase halves and a two-step merkle branch
        self.coinb1 = os.urandom(42).hex()
        self.coinb2 = os.urandom(30).hex()
        self.branch = [os.urandom(32).hex(), os.urandom(32).hex()]
        self.prev_rpc = os.urandom(32)  # internal byte order
        self.version = 0x20000002
        self.nbits = 0x1e0ffff0
        self.ntime = int(time.time())
        threading.Thread(target=self.serve, daemon=True).start()

    def prev_stratum(self):
        # stratum prevhash: the RPC display hex with its 8 dwords in reverse order
        disp = self.prev_rpc[::-1].hex()
        return "".join(disp[i * 8:(i + 1) * 8] for i in range(7, -1, -1))

    def serve(self):
        self.srv.settimeout(0.5)
        while not self.stop:
            try:
                conn, _ = self.srv.accept()
            except socket.timeout:
                continue
            threading.Thread(target=self.session, args=(conn,), daemon=True).start()

    def send(self, conn, obj):
        try:
            conn.sendall((json.dumps(obj) + "\n").encode())
        except OSError:
            pass  # the client hung up (setstratum false)

    def check_share(self, en2, ntime, nonce):
        coinbase = bytes.fromhex(self.coinb1 + EN1 + en2 + self.coinb2)
        root = dsha(coinbase)
        for b in self.branch:
            root = dsha(root + bytes.fromhex(b))
        header = struct.pack("<I", self.version) + self.prev_rpc + root + \
            struct.pack("<III", int(ntime, 16), self.nbits, int(nonce, 16))
        h = hashlib.scrypt(header, salt=header, n=1024, r=1, p=1, dklen=32)
        return int.from_bytes(h, "little") <= DIFF1 * MULT / DIFF

    def session(self, conn):
        buf = b""
        conn.settimeout(0.5)
        while not self.stop:
            try:
                data = conn.recv(4096)
            except socket.timeout:
                continue
            except OSError:
                return
            if not data:
                return
            buf += data
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                msg = json.loads(line)
                m = msg.get("method")
                if m == "mining.subscribe":
                    self.send(conn, {"id": msg["id"], "result": [[["mining.notify", "1"]], EN1, 4], "error": None})
                elif m == "mining.authorize":
                    user = msg["params"][0]
                    with self.lock:
                        self.users.append(user)
                    self.send(conn, {"id": msg["id"], "result": True, "error": None})
                    self.send(conn, {"id": None, "method": "mining.set_difficulty", "params": [DIFF]})
                    self.send(conn, {"id": None, "method": "mining.notify", "params": [
                        "job1", self.prev_stratum(), self.coinb1, self.coinb2, self.branch,
                        "%08x" % self.version, "%08x" % self.nbits, "%08x" % self.ntime, True]})
                elif m == "mining.submit":
                    _, job, en2, ntime, nonce = msg["params"]
                    ok = job == "job1" and ntime == "%08x" % self.ntime and self.check_share(en2, ntime, nonce)
                    control = ok and self.check_share(en2, ntime, "%08x" % (int(nonce, 16) ^ 0x80000000))
                    with self.lock:
                        if control:
                            self.control_pass += 1
                        if ok:
                            self.good += 1
                        else:
                            self.bad += 1
                    self.send(conn, {"id": msg["id"], "result": ok, "error": None if ok else [23, "low difficulty share", None]})


class StratumTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def run_test(self):
        node = self.nodes[0]
        assert_equal(node.getstratuminfo(), {"running": False})
        pool = FakePool()
        payout = "mjTkW3DjgyZck4KbiRusZsqTgaYTxdSz6z"  # any valid regtest address

        for impl in ["generic", "sse2", "avx", "avx2"]:
            self.restart_node(0, ["-minerscrypt=" + impl])
            node = self.nodes[0]
            with open(os.path.join(node.datadir, self.chain, "debug.log"), encoding="utf8") as f:
                if "-minerscrypt=%s is not supported" % impl in f.read():
                    self.log.info("%s: not supported by this CPU, skipped" % impl)
                    continue
            good0, bad0, ctl0 = pool.good, pool.bad, pool.control_pass
            info = node.setstratum(True, "127.0.0.1:%d" % pool.port, payout + ".w1", 2)
            assert_equal(info["running"], True)
            deadline = time.time() + 120
            while time.time() < deadline and pool.good - good0 < 8:
                time.sleep(0.5)
            node.getstratuminfo()  # the hash meter needs two samples 5s apart
            time.sleep(5.5)
            info = node.getstratuminfo()
            assert info["connected"] and info["authorized"], info
            assert pool.good - good0 >= 8, "only %d good shares" % (pool.good - good0)
            assert_equal(pool.bad - bad0, 0)
            # negative control: the bound is real, so the same share with another nonce almost always fails
            assert pool.control_pass - ctl0 <= max(2, (pool.good - good0) // 20), "share check is not selective"
            assert info["sharesaccepted"] >= 8 and info["sharesrejected"] == 0, info
            assert_equal(pool.users[-1], payout + ".w1")
            self.log.info("%s: %d shares, every one valid by Python's own scrypt; %.0f H/s" % (impl, pool.good - good0, info["hashespersec"]))
            node.setstratum(False)
            assert_equal(node.getstratuminfo(), {"running": False})

        self.log.info("stopping a client stuck connecting to a dead pool returns promptly (review fix)")
        node.setstratum(True, "10.255.255.1:3033", payout, 1)  # unroutable: the connect never answers
        time.sleep(2)
        t0 = time.time()
        node.setstratum(False)
        assert time.time() - t0 < 5, "setstratum false took %.1fs" % (time.time() - t0)
        assert_equal(node.getstratuminfo(), {"running": False})

        self.log.info("a bad payout address and a bad endpoint are refused")
        assert_raises_rpc_error(-5, "Invalid Dobbscoin payout address", node.setstratum, True, "127.0.0.1:%d" % pool.port, "notanaddress")
        assert_raises_rpc_error(-8, "host:port", node.setstratum, True, "nocolon", payout)
        assert_raises_rpc_error(-8, "needs a pool", node.setstratum, True)
        pool.stop = True


if __name__ == '__main__':
    StratumTest().main()
