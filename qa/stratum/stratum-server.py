#!/usr/bin/env python3
# Copyright (c) 2026 The Dobbscoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""A minimal stratum v1 pool, for testing the wallet's stratum client
(src/stratum.cpp) without a real pool.

It speaks the dialect the client was written against (Miningcore's, see the
comments in stratum.cpp): mining.subscribe returns a 4-byte extranonce1 and
extranonce2_size 4; mining.notify carries a job built from a node's
getblocktemplate; mining.submit is [worker, job, extranonce2, ntime, nonce].

Every submitted share is re-built and scrypt-hashed HERE, independently of the
client (hashlib.scrypt, N=1024 r=1 p=1), and accepted only if

    scrypt(header) <= DIFF1_TARGET * SHARE_MULTIPLIER / difficulty

with DIFF1_TARGET = 0xffff * 2**208 and SHARE_MULTIPLIER = 65536, the bound
stratum.cpp documents for Miningcore. At difficulty d a share takes about
65536 * d hashes. A share that also meets the block's nBits is sent to the
node with submitblock, and a new job follows (clean_jobs = true).

Usage (the node must be reachable over RPC; testnet needs one peer for GBT):

    qa/stratum/stratum-server.py --rpcport 29422 --rpcuser u --rpcpassword p \\
        --port 29450 --difficulty 1 [--max-blocks N] [--stats-file f.json]

It writes one line per event to stdout and, with --stats-file, a JSON summary
after every share. It stops on SIGTERM/SIGINT.
"""

import argparse
import base64
import hashlib
import json
import signal
import socketserver
import struct
import sys
import threading
import time
import urllib.request
from fractions import Fraction

DIFF1_TARGET = 0xffff * 2 ** 208
SHARE_MULTIPLIER = 65536


def log(msg):
    print("%s stratum-server: %s" % (time.strftime("%H:%M:%S"), msg), flush=True)


def sha256d(b):
    return hashlib.sha256(hashlib.sha256(b).digest()).digest()


def scrypt_pow(header80):
    """(BOB) proof-of-work hash as an integer (uint256, little-endian bytes)."""
    h = hashlib.scrypt(header80, salt=header80, n=1024, r=1, p=1, dklen=32)
    return int.from_bytes(h, "little")


def compact_to_target(nbits):
    exp = nbits >> 24
    mant = nbits & 0x007fffff
    return mant >> (8 * (3 - exp)) if exp <= 3 else mant << (8 * (exp - 3))


def varint(n):
    if n < 0xfd:
        return bytes([n])
    if n <= 0xffff:
        return b"\xfd" + struct.pack("<H", n)
    if n <= 0xffffffff:
        return b"\xfe" + struct.pack("<I", n)
    return b"\xff" + struct.pack("<Q", n)


def script_height(h):
    """CScript() << nHeight, as the 0.10 node builds and checks it (BIP34)."""
    if h == 0:
        return b"\x00"
    if 1 <= h <= 16:
        return bytes([0x50 + h])
    out = bytearray()
    v = h
    while v:
        out.append(v & 0xff)
        v >>= 8
    if out[-1] & 0x80:
        out.append(0)
    return bytes([len(out)]) + bytes(out)


class Rpc:
    def __init__(self, host, port, user, password):
        self.url = "http://%s:%d/" % (host, port)
        self.auth = "Basic " + base64.b64encode(("%s:%s" % (user, password)).encode()).decode()
        self.lock = threading.Lock()
        self.n = 0

    def __call__(self, method, *params):
        with self.lock:
            self.n += 1
            body = json.dumps({"id": self.n, "method": method, "params": list(params)}).encode()
        req = urllib.request.Request(self.url, data=body, headers={"Authorization": self.auth, "Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(req, timeout=30) as r:
                reply = json.loads(r.read())
        except urllib.error.HTTPError as e:  # JSON-RPC errors come back as HTTP 500
            reply = json.loads(e.read())
        if reply.get("error"):
            raise RuntimeError("%s: %s" % (method, reply["error"]))
        return reply["result"]


class Job:
    def __init__(self, job_id, tmpl, payout_script, en1_size, en2_size):
        self.job_id = job_id
        self.height = tmpl["height"]
        self.prevhash_rpc = tmpl["previousblockhash"]
        self.version = tmpl["version"]
        self.nbits = int(tmpl["bits"], 16)
        self.ntime = tmpl["curtime"]
        self.txs = tmpl.get("transactions", [])
        # coinbase = coinb1 | extranonce1 | extranonce2 | coinb2
        extra = en1_size + en2_size
        script_sig_head = script_height(self.height) + bytes([extra])  # then a push of en1+en2
        script_len = len(script_sig_head) + extra
        self.coinb1 = (struct.pack("<i", 1) + b"\x01" + b"\x00" * 32 + b"\xff\xff\xff\xff"
                       + varint(script_len) + script_sig_head)
        self.coinb2 = (b"\xff\xff\xff\xff" + b"\x01" + struct.pack("<q", tmpl["coinbasevalue"])
                       + varint(len(payout_script)) + payout_script + struct.pack("<I", 0))
        # merkle branch for the coinbase (index 0), internal byte order
        level = [None] + [bytes.fromhex(t["hash"])[::-1] for t in self.txs]
        self.branch = []
        while len(level) > 1:
            self.branch.append(level[1])
            if len(level) % 2:
                level.append(level[-1])
            nxt = [None]
            for i in range(2, len(level), 2):
                nxt.append(sha256d(level[i] + level[i + 1]))
            level = nxt

    def notify_params(self, clean):
        words = [self.prevhash_rpc[i:i + 8] for i in range(0, 64, 8)]
        prev_stratum = "".join(reversed(words))  # the client reverses the 8 words back
        return [self.job_id, prev_stratum, self.coinb1.hex(), self.coinb2.hex(),
                [b.hex() for b in self.branch], "%08x" % (self.version & 0xffffffff),
                "%08x" % self.nbits, "%08x" % self.ntime, clean]

    def build(self, en1, en2, ntime_hex, nonce_hex):
        coinbase = self.coinb1 + en1 + en2 + self.coinb2
        root = sha256d(coinbase)
        for b in self.branch:
            root = sha256d(root + b)
        header = (struct.pack("<i", self.version) + bytes.fromhex(self.prevhash_rpc)[::-1] + root
                  + struct.pack("<I", int(ntime_hex, 16)) + struct.pack("<I", self.nbits)
                  + struct.pack("<I", int(nonce_hex, 16)))
        return header, coinbase


class Pool:
    def __init__(self, args):
        self.args = args
        self.rpc = Rpc(args.rpchost, args.rpcport, args.rpcuser, args.rpcpassword)
        self.lock = threading.RLock()
        self.difficulty = Fraction(args.difficulty).limit_denominator(1 << 20)
        self.share_target = DIFF1_TARGET * SHARE_MULTIPLIER / self.difficulty
        self.jobs = {}
        self.job = None
        self.next_job = 1
        self.clients = []
        self.payout_script = None
        self.next_en1 = 0x10000000
        self.stats = {"shares_accepted": 0, "shares_rejected": 0, "reject_reasons": {},
                      "blocks_submitted": 0, "blocks_accepted": 0, "block_hashes": [],
                      "hashes_estimate": 0, "first_share_time": None, "last_share_time": None}
        self.seen = set()

    def write_stats(self):
        if self.args.stats_file:
            with open(self.args.stats_file + ".tmp", "w") as f:
                json.dump(self.stats, f, indent=1)
            import os
            os.replace(self.args.stats_file + ".tmp", self.args.stats_file)

    def set_payout(self, address):
        with self.lock:
            if self.payout_script is None:
                info = self.rpc("validateaddress", address)
                if not info.get("isvalid"):
                    return False
                # validateaddress only shows scriptPubKey for the node's own
                # addresses; a one-output raw transaction gives it for any.
                raw = self.rpc("createrawtransaction", [], {address: 1})
                self.payout_script = bytes.fromhex(self.rpc("decoderawtransaction", raw)["vout"][0]["scriptPubKey"]["hex"])
                log("payout address %s" % address)
        return True

    def new_job(self, clean):
        with self.lock:
            if self.payout_script is None:
                return None
            tmpl = self.rpc("getblocktemplate")
            job = Job("%x" % self.next_job, tmpl, self.payout_script, 4, 4)
            self.next_job += 1
            self.jobs[job.job_id] = job
            self.job = job
            log("job %s height %d prev %s.. nbits %08x, %d txs" % (job.job_id, job.height, job.prevhash_rpc[:16], job.nbits, len(job.txs)))
            for c in list(self.clients):
                c.send_notify(job, clean)
            return job

    def poll(self):
        """New job when the tip moves (a block from elsewhere)."""
        while True:
            time.sleep(1)
            try:
                with self.lock:
                    job = self.job
                if job is not None and self.rpc("getbestblockhash") != job.prevhash_rpc:
                    self.new_job(True)
            except Exception as e:
                log("poll error: %s" % e)

    def submit(self, client, params):
        worker, job_id, en2_hex, ntime_hex, nonce_hex = params[:5]
        with self.lock:
            job = self.jobs.get(job_id)
            if job is None:
                return self.reject("job not found")
            if len(en2_hex) != 8 or len(ntime_hex) != 8 or len(nonce_hex) != 8:
                return self.reject("malformed")
            key = (job_id, client.en1.hex(), en2_hex, ntime_hex, nonce_hex)
            if key in self.seen:
                return self.reject("duplicate share")
            self.seen.add(key)
        header, coinbase = job.build(client.en1, bytes.fromhex(en2_hex), ntime_hex, nonce_hex)
        pow_hash = scrypt_pow(header)
        if pow_hash > self.share_target:
            return self.reject("low difficulty share (hash %064x)" % pow_hash)
        now = time.time()
        with self.lock:
            self.stats["shares_accepted"] += 1
            self.stats["hashes_estimate"] = int(self.stats["shares_accepted"] * 65536 * self.difficulty)
            self.stats["first_share_time"] = self.stats["first_share_time"] or now
            self.stats["last_share_time"] = now
        log("share ACCEPTED from %s job %s nonce %s hash %064x" % (worker, job_id, nonce_hex, pow_hash))
        if pow_hash <= compact_to_target(job.nbits) and (self.args.max_blocks < 0 or self.stats["blocks_submitted"] < self.args.max_blocks):
            block = header + varint(1 + len(job.txs)) + coinbase + b"".join(bytes.fromhex(t["data"]) for t in job.txs)
            block_hash = sha256d(header)[::-1].hex()
            with self.lock:
                self.stats["blocks_submitted"] += 1
            try:
                res = self.rpc("submitblock", block.hex())
            except Exception as e:
                res = str(e)
            ok = res is None
            with self.lock:
                if ok:
                    self.stats["blocks_accepted"] += 1
                    self.stats["block_hashes"].append(block_hash)
            log("BLOCK %s at height %d submitted: %s" % (block_hash, job.height, "ACCEPTED" if ok else "REJECTED (%s)" % res))
            self.new_job(True)
        self.write_stats()
        return True, None

    def reject(self, reason):
        with self.lock:
            self.stats["shares_rejected"] += 1
            self.stats["reject_reasons"][reason.split(" (")[0]] = self.stats["reject_reasons"].get(reason.split(" (")[0], 0) + 1
        log("share REJECTED: %s" % reason)
        self.write_stats()
        return False, [23, reason, None]


class Handler(socketserver.StreamRequestHandler):
    def send(self, obj):
        data = (json.dumps(obj) + "\n").encode()
        with self.wlock:
            self.wfile.write(data)
            self.wfile.flush()

    def send_notify(self, job, clean):
        try:
            self.send({"id": None, "method": "mining.notify", "params": job.notify_params(clean)})
        except OSError:
            pass

    def handle(self):
        pool = self.server.pool
        self.wlock = threading.Lock()
        with pool.lock:
            self.en1 = struct.pack(">I", pool.next_en1)
            pool.next_en1 += 1
        log("client connected from %s:%d" % self.client_address)
        try:
            for raw in self.rfile:
                line = raw.decode().strip()
                if not line:
                    continue
                msg = json.loads(line)
                method, mid, params = msg.get("method"), msg.get("id"), msg.get("params", [])
                if method == "mining.subscribe":
                    self.send({"id": mid, "result": [[["mining.notify", "1"]], self.en1.hex(), 4], "error": None})
                elif method == "mining.authorize":
                    ok = pool.set_payout(params[0])
                    self.send({"id": mid, "result": ok, "error": None})
                    if ok:
                        with pool.lock:
                            pool.clients.append(self)
                        self.send({"id": None, "method": "mining.set_difficulty", "params": [float(pool.difficulty)]})
                        job = pool.job or pool.new_job(True)
                        if job is not None and pool.job is job:
                            self.send_notify(job, True)
                elif method == "mining.submit":
                    ok, err = pool.submit(self, params)
                    self.send({"id": mid, "result": ok if ok else None, "error": err})
                else:
                    self.send({"id": mid, "result": None, "error": [20, "unknown method", None]})
        except (OSError, ValueError) as e:
            log("client error: %s" % e)
        finally:
            with pool.lock:
                if self in pool.clients:
                    pool.clients.remove(self)
            log("client disconnected")


class Server(socketserver.ThreadingMixIn, socketserver.TCPServer):
    allow_reuse_address = True
    daemon_threads = True


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rpchost", default="127.0.0.1")
    ap.add_argument("--rpcport", type=int, required=True)
    ap.add_argument("--rpcuser", required=True)
    ap.add_argument("--rpcpassword", required=True)
    ap.add_argument("--bind", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=29450)
    ap.add_argument("--difficulty", type=float, default=1.0, help="share difficulty (about 65536*d hashes per share)")
    ap.add_argument("--max-blocks", type=int, default=-1, help="submit at most N blocks (-1 = no limit)")
    ap.add_argument("--stats-file")
    args = ap.parse_args()

    pool = Pool(args)
    server = Server((args.bind, args.port), Handler)
    server.pool = pool
    signal.signal(signal.SIGTERM, lambda *a: (pool.write_stats(), sys.exit(0)))
    threading.Thread(target=pool.poll, daemon=True).start()
    log("listening on %s:%d, share difficulty %s (target %064x)" % (args.bind, args.port, float(pool.difficulty), int(pool.share_target)))
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    pool.write_stats()


if __name__ == "__main__":
    main()
