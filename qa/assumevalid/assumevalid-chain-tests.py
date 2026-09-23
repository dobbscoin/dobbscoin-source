#!/usr/bin/env python3
"""On-chain -assumevalid tests for (BOB): does ConnectBlock actually skip, and
does it refuse to skip when it should not?

-assumevalid can only fire on a node that already holds the named block in its
index when it connects the block that hash is meant to cover. That ordering
only happens during headers-first sync, so a single node fed blocks by
submitblock can never exercise it. Hence two nodes: A mines a chain by hand
(the standard build has the wallet compiled out, so there is no setgenerate),
B syncs it over p2p with -assumevalid set different ways.

The observable is the -debug=assumevalid log line in ConnectBlock. What that
line proves on its own is that the gate opened; what proves the gate controls
real verification is the sabotage run described in the README.

Usage: ./assumevalid-chain-tests.py <A.conf> <datadir-B> <dobbscoind> <port-A> <rpcport-B>
"""
import os
import re
import socket
import subprocess
import sys
import time

socket.setdefaulttimeout(60)

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "cltv"))
from cltvlib import Key, Rpc, mine_block, spend, txid

SPACING = 600          # nTargetSpacing_V3; see qa/cltv/README.md on LWMA-3
FEE     = 100000
MATURITY = 100
SPENDS  = 10           # blocks carrying a real signature to verify
TIP_H   = 120
MID_H   = 110

results = []
clock = [0]
coinbases = []


def check(name, ok, detail=""):
    print("  [%s] %-52s %s" % ("PASS" if ok else "FAIL", name, detail))
    results.append(ok)


def mine(rpc, txs, key):
    clock[0] += SPACING
    rpc("setmocktime", clock[0])
    res, cb_txid, height, cb_value = mine_block(rpc, txs, key)
    if res is not None:
        raise RuntimeError("block at height %d rejected: %s" % (height, res))
    coinbases.append((cb_txid, cb_value))
    return height


def build_chain(rpc, key):
    """101 maturity blocks, then SPENDS blocks each carrying one real P2PK spend,
    then filler to TIP_H. Without the spends no script check would ever run and
    the whole suite would pass while proving nothing."""
    clock[0] = int(time.time()) - 400 * SPACING
    while rpc("getblockcount") < MATURITY + 1:
        mine(rpc, [], key)
    spent = 0
    while spent < SPENDS:
        prev_txid, prev_value = coinbases[spent]
        tx = spend(prev_txid, 0, key.p2pk_script(), key,
                   key.p2pk_script(), prev_value - FEE)
        mine(rpc, [tx], key)
        spent += 1
    while rpc("getblockcount") < TIP_H:
        mine(rpc, [], key)
    return rpc("getblockcount")


class NodeB(object):
    def __init__(self, daemon, datadir, rpcport, portA, extra=()):
        self.daemon, self.datadir, self.rpcport, self.portA = daemon, datadir, rpcport, portA
        self.extra = extra
        self.log = os.path.join(datadir, "regtest", "debug.log")

    def conf(self):
        os.makedirs(self.datadir, exist_ok=True)
        with open(os.path.join(self.datadir, "dobbscoin.conf"), "w") as f:
            f.write("regtest=1\nrpcuser=av\nrpcpassword=test\nrpcport=%d\n" % self.rpcport)

    def start(self, assumevalid, wait_height=None, expect_fail=False):
        """Start B from an empty datadir. Returns an Rpc on success, or (with
        expect_fail) the daemon's output as a string once it has refused to
        start, or None if it came up anyway.

        dobbscoind -daemon forks without closing the stdio it inherited, so a
        subprocess pipe here would never see EOF and would block forever. The
        daemon gets a file instead.
        """
        subprocess.call(["rm", "-rf", self.datadir])
        self.conf()
        args = [self.daemon, "-regtest", "-datadir=" + self.datadir,
                "-rpcuser=av", "-rpcpassword=test", "-rpcport=%d" % self.rpcport,
                "-listen=0", "-discover=0", "-debug=assumevalid",
                "-connect=127.0.0.1:%d" % self.portA] + list(self.extra)
        if assumevalid is not None:
            args.append("-assumevalid=" + assumevalid)
        if expect_fail:
            return self.start_refused(args)
        startlog = os.path.join(self.datadir, "start.out")
        with open(startlog, "w") as out:
            subprocess.call(args + ["-daemon"], stdout=out, stderr=subprocess.STDOUT)
        rpc = Rpc(os.path.join(self.datadir, "dobbscoin.conf"))
        deadline = time.time() + 180
        while time.time() < deadline:
            try:
                h = rpc("getblockcount")
                if wait_height is None or h >= wait_height:
                    return rpc
            except Exception:
                pass
            time.sleep(1)
        return open(startlog).read() or "(node never answered RPC)"

    def start_refused(self, args, timeout=60):
        """Run the daemon in the foreground and wait for it to exit.

        Not -daemon: the -daemon parent prints "Dobbscoin server starting" and
        exits 0 before the forked child has even reached AppInit2, so the start
        log is non-empty long before the child writes its InitError. Polling
        that file for "anything written yet" raced the child and, whenever the
        poll won, read only the "starting" line (the intermittent "non-hex
        rejected" failure). In the foreground the process exits after
        InitError, so its whole output is in hand when run() returns.
        """
        try:
            p = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               text=True, timeout=timeout)
        except subprocess.TimeoutExpired:
            return None  # still running: it accepted the argument (run() killed it)
        return p.stdout or "(exited %d with no output)" % p.returncode

    def stop(self):
        pidfile = os.path.join(self.datadir, "regtest", "dobbscoind.pid")
        pid = int(open(pidfile).read().strip()) if os.path.exists(pidfile) else None
        try:
            Rpc(os.path.join(self.datadir, "dobbscoin.conf"))("stop")
        except Exception:
            pass
        for _ in range(30):
            if pid is None:
                break
            try:
                os.kill(pid, 0)
            except OSError:
                return
            time.sleep(1)
        if pid is not None:
            try:
                os.kill(pid, 9)
            except OSError:
                pass
            time.sleep(1)

    def skipped(self):
        if not os.path.exists(self.log):
            return []
        return [int(m) for m in re.findall(
            r"assumevalid: skipping script checks for block (\d+)", open(self.log).read())]

    def logged(self, text):
        return os.path.exists(self.log) and text in open(self.log).read()


def main():
    confA, datadirB, daemon, portA, rpcportB = sys.argv[1:6]
    rpcA = Rpc(confA)
    key = Key()

    print("==> node A: building the chain")
    tip_h = build_chain(rpcA, key)
    tip = rpcA("getblockhash", tip_h)
    mid = rpcA("getblockhash", MID_H)
    genesis = rpcA("getblockhash", 0)
    blk = rpcA("getblock", rpcA("getblockhash", MATURITY + 2))
    check("chain carries real spends to verify", len(blk["tx"]) > 1,
          "block %d has %d txs" % (MATURITY + 2, len(blk["tx"])))
    print("    tip height %d, hash %s" % (tip_h, tip))

    B = NodeB(daemon, datadirB, int(rpcportB), int(portA))

    print("\n==> 1. -assumevalid=<tip>: every block is an ancestor or the block itself")
    rpc = B.start(tip, tip_h)
    check("B synced to A's tip", rpc("getblockcount") == tip_h, "height %d" % rpc("getblockcount"))
    s1 = B.skipped()
    check("script checks skipped for the whole chain", len(s1) == tip_h,
          "%d of %d blocks" % (len(s1), tip_h))
    check("the assumevalid block itself is covered", tip_h in s1)
    check("startup logged the assumption", B.logged("Assuming ancestors of block %s" % tip))
    B.stop()

    print("\n==> 2. -assumevalid=<block %d>: the skip stops there" % MID_H)
    rpc = B.start(mid, tip_h)
    check("B synced to A's tip", rpc("getblockcount") == tip_h, "height %d" % rpc("getblockcount"))
    s2 = B.skipped()
    check("highest skipped height is %d" % MID_H, max(s2) == MID_H, "max %d" % max(s2))
    check("blocks above it were verified", len(s2) == MID_H, "%d skipped" % len(s2))
    B.stop()

    print("\n==> 3. -assumevalid=0: nothing is skipped and the node still syncs")
    rpc = B.start("0", tip_h)
    check("B synced to A's tip", rpc("getblockcount") == tip_h, "height %d" % rpc("getblockcount"))
    check("no skips", B.skipped() == [])
    check("startup logged full verification", B.logged("Verifying scripts for all blocks"))
    B.stop()

    print("\n==> 4. guard: a hash that is in no index skips nothing")
    rpc = B.start("f" * 64, tip_h)
    check("B synced to A's tip", rpc("getblockcount") == tip_h, "height %d" % rpc("getblockcount"))
    check("no skips", B.skipped() == [])
    B.stop()

    print("\n==> 5. guard: an ancestor of nothing we are connecting skips nothing")
    rpc = B.start(genesis, tip_h)
    check("B synced to A's tip", rpc("getblockcount") == tip_h, "height %d" % rpc("getblockcount"))
    check("genesis as assumevalid covers no block", B.skipped() == [])
    B.stop()

    print("\n==> 6. malformed -assumevalid is refused at startup")
    err = B.start("deadbeef", expect_fail=True)
    check("short hash rejected", isinstance(err, str) and "Invalid block hash for -assumevalid" in err,
          (err if isinstance(err, str) else "node started anyway").strip()[:70])
    err = B.start("z" * 64, expect_fail=True)
    check("non-hex rejected", isinstance(err, str) and "Invalid block hash for -assumevalid" in err,
          (err if isinstance(err, str) else "node started anyway").strip()[:70])
    B.stop()

    print("\n==> 7. help text carries the mainnet default and does not need SelectParams")
    out = subprocess.run([daemon, "-?"], capture_output=True, text=True).stdout
    line = [l for l in out.splitlines() if "-assumevalid" in l]
    check("dobbscoind -? documents the option", bool(line))
    check("and shows the mainnet default",
          bool(line) and "179ef9bac5e0a36e4163c893378086fe779045183ceb581106408de43090df3b" in line[0])

    print("\n%s  %d/%d passed" % ("PASS" if all(results) else "FAIL",
                                  sum(1 for r in results if r), len(results)))
    sys.exit(0 if all(results) else 1)


if __name__ == "__main__":
    main()
