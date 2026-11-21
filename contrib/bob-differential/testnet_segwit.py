#!/usr/bin/env python3
"""(BOB) SegWit/CSV activation rehearsal on a private copy of the 0.14 testnet, with real
proof-of-work: three nodes on the 360-block chain the testnet differential built.
  old  0.14.1, never upgraded (it must keep following: a soft fork)
  A    EndTimes, miner, wallet "m", SegWit+CSV active from ACT
  B    EndTimes, wallet "w", SegWit+CSV active from ACT, syncs from A
Checks: B refuses bech32 before ACT; A mines through ACT; after ACT a tbob1 payment to B,
B's witness spend back, and a v2 BIP68 spend confirm; blocks carry the witness
commitment; 0.14.1 follows every block; a block 0.14.1 mines after ACT is valid for both
EndTimes nodes; all three chains end identical. Progress: <work>/progress.log
usage: testnet_segwit.py <endtimes-src> <ref-0.14.1> <testnet-diff-dir> <workdir>
"""
import json, os, shutil, subprocess, sys, time, urllib.request
from decimal import Decimal

NEW, REF, SRC, W = (os.path.abspath(a) for a in sys.argv[1:5])
ACT, TAIL = 380, 20
LOG = os.path.join(W, "progress.log")
problems = []


def say(*a):
    line = time.strftime("%H:%M:%S ") + " ".join(str(x) for x in a)
    print(line, flush=True)
    open(LOG, "a").write(line + "\n")


class Node:
    def __init__(self, name, argv, rpcport, env=None):
        self.name, self.port = name, rpcport
        self.proc = subprocess.Popen(argv + ["-testnet", "-rpcuser=t", "-rpcpassword=tsw", "-rpcport=%d" % rpcport,
                                             "-server=1", "-printtoconsole=0", "-dnsseed=0"],
                                     stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=env)
        for _ in range(180):
            try:
                self.call("getblockcount"); return
            except Exception:
                time.sleep(1)
        raise SystemExit("%s did not start" % name)

    def call(self, method, *params, wallet=None):
        url = "http://127.0.0.1:%d/%s" % (self.port, "wallet/" + wallet if wallet else "")
        req = urllib.request.Request(url, data=json.dumps({"id": 1, "method": method, "params": list(params)}).encode(),
                                     headers={"Authorization": "Basic dDp0c3c=", "Content-Type": "application/json"})
        try:
            r = json.loads(urllib.request.urlopen(req, timeout=300).read(), parse_float=Decimal)
        except urllib.error.HTTPError as e:
            r = json.loads(e.read(), parse_float=Decimal)
        if r.get("error"):
            raise RuntimeError("%s %s: %s" % (self.name, method, r["error"]))
        return r["result"]

    def stop(self):
        try: self.call("stop")
        except Exception: pass
        try: self.proc.wait(60)
        except Exception: self.proc.kill()


def wait_for(pred, what, timeout):
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            if pred(): return True
        except Exception:
            pass
        time.sleep(5)
    problems.append(what); say("PROBLEM:", what)
    return False


def mine_to(h, timeout=6 * 3600):
    A.call("setgenerate", True, 4, wallet="m")
    last = -1
    t0 = time.time()
    while A.call("getblockcount") < h and time.time() - t0 < timeout:
        cur = A.call("getblockcount")
        if cur != last and cur % 5 == 0:
            say("height A=%d B=%d old=%d" % (cur, B.call("getblockcount"), old.call("getblockcount")))
        last = cur
        time.sleep(10)
    A.call("setgenerate", False, wallet="m")
    wait_for(lambda: old.call("getblockcount") >= h and B.call("getblockcount") >= h, "old/B did not reach %d" % h, 300)


def confirm(txid, node, wallet, what):
    A.call("setgenerate", True, 4, wallet="m")
    ok = wait_for(lambda: node.call("gettransaction", txid, wallet=wallet)["confirmations"] >= 1, what + " did not confirm", 3600)
    A.call("setgenerate", False, wallet="m")
    if ok:
        say("confirmed:", what, txid[:16], "in", node.call("gettransaction", txid, wallet=wallet)["blockhash"][:16])


if os.path.exists(W):
    shutil.rmtree(W)
os.makedirs(W)
shutil.copytree(os.path.join(SRC, "old"), os.path.join(W, "old"))
shutil.copytree(os.path.join(SRC, "new"), os.path.join(W, "a"))
os.makedirs(os.path.join(W, "b"))
for d in ("old", "a"):
    for f in ("peers.dat", "banlist.dat", ".lock"):
        p = os.path.join(W, d, "testnet3", f)
        if os.path.exists(p): os.remove(p)

act = ["-segwitheight=%d" % ACT, "-csvheight=%d" % ACT]
env = dict(os.environ, LD_LIBRARY_PATH=os.path.join(REF, "lib"))
old = Node("old", [os.path.join(REF, "dobbscoind"), "-datadir=" + os.path.join(W, "old"), "-port=39985", "-bind=127.0.0.1", "-listen=1"], 39981, env)
A = Node("A", [os.path.join(NEW, "dobbscoind"), "-datadir=" + os.path.join(W, "a"), "-port=39975", "-bind=127.0.0.1", "-listen=1",
               "-connect=127.0.0.1:39985"] + act, 39982)
B = Node("B", [os.path.join(NEW, "dobbscoind"), "-datadir=" + os.path.join(W, "b"), "-listen=0",
               "-connect=127.0.0.1:39975", "-connect=127.0.0.1:39985"] + act, 39983)
rc = 1
try:
    A.call("loadwallet", "m")
    B.call("createwallet", "w")
    # The copied chain's tip is over a day old, so A and old count as in initial block download
    # and ignore getheaders. One fresh block from A ends that; then B can sync.
    A.call("setgenerate", True, 4, wallet="m")
    wait_for(lambda: A.call("getblockcount") >= 361, "A did not mine its first block", 3600)
    A.call("setgenerate", False, wallet="m")
    wait_for(lambda: B.call("getblockcount") == A.call("getblockcount") == old.call("getblockcount"), "B did not sync the chain", 900)
    say("start: A=%d B=%d old=%d, activation at %d" % (A.call("getblockcount"), B.call("getblockcount"), old.call("getblockcount"), ACT))

    try:
        B.call("getnewaddress", "", "bech32", wallet="w")
        problems.append("B handed out a bech32 address before activation"); say("PROBLEM: bech32 before activation")
    except RuntimeError as e:
        say("before activation B refuses bech32: ok (%s)" % str(e)[-60:])

    mine_to(ACT + 1)
    tb = B.call("getnewaddress", "", "bech32", wallet="w")
    say("after activation B's bech32 address:", tb)
    if not tb.startswith("tbob1"): problems.append("bech32 prefix %s" % tb[:6])

    pay = [A.call("sendtoaddress", tb, 2, wallet="m") for _ in range(5)]
    confirm(pay[-1], B, "w", "5 payments of 2 BOB to tbob1")
    back = B.call("sendtoaddress", A.call("getnewaddress", "", "legacy", wallet="m"), 3, wallet="w")
    raw = B.call("getrawtransaction", back, True)
    if not any(v.get("txinwitness") for v in raw["vin"]): problems.append("B's spend carries no witness")
    confirm(back, B, "w", "B's witness spend back to A")

    u = [x for x in A.call("listunspent", 6, 9999999, wallet="m") if x["amount"] > 1][0]
    rawtx = A.call("createrawtransaction", [{"txid": u["txid"], "vout": u["vout"], "sequence": 5}],
                   {A.call("getnewaddress", "", "legacy", wallet="m"): float(u["amount"] - Decimal("0.01"))}, 0, False)
    signed = A.call("signrawtransactionwithwallet", "02" + rawtx[2:], wallet="m")["hex"]
    csv = A.call("sendrawtransaction", signed)
    confirm(csv, A, "m", "v2 BIP68 spend (relative lock 5)")

    mine_to(ACT + TAIL)

    say("0.14.1 mines one block after activation")
    h0 = old.call("getblockcount")
    old.call("setgenerate", True, 1)
    wait_for(lambda: old.call("getblockcount") > h0, "0.14.1 did not mine a block", 3 * 3600)
    old.call("setgenerate", False)
    bh = old.call("getbestblockhash")
    res = A.call("submitblock", old.call("getblock", bh, False))
    wait_for(lambda: A.call("getbestblockhash") == bh and B.call("getbestblockhash") == bh,
             "EndTimes did not take 0.14.1's block %s (submitblock: %s)" % (bh[:16], res), 300)
    say("0.14.1 block %d %s: A and B have it" % (h0 + 1, bh[:16]))

    n = A.call("getblockcount")
    for h in range(n + 1):
        hs = {x.call("getblockhash", h) for x in (A, B, old)}
        if len(hs) != 1:
            problems.append("chains differ at %d" % h); say("PROBLEM: chains differ at", h); break
    wit = commit = 0
    for h in range(ACT, n + 1):
        blk = A.call("getblock", A.call("getblockhash", h), 2)
        wit += sum(1 for tx in blk["tx"][1:] for v in tx["vin"] if v.get("txinwitness"))
        commit += any(o["scriptPubKey"]["hex"].startswith("6a24aa21a9ed") for o in blk["tx"][0]["vout"])
    say("blocks %d..%d: %d witness inputs, %d with a witness commitment; B balance %s" %
        (ACT, n, wit, commit, B.call("getbalances", wallet="w")["mine"]["trusted"]))
    if wit == 0: problems.append("no witness inputs mined")
    say("RESULT: %s" % ("PASS, identical chains 0..%d across activation at %d" % (n, ACT) if not problems else "FAIL: %s" % problems))
    rc = 0 if not problems else 1
except Exception as e:
    say("FAIL: exception", repr(e)[:300])
finally:
    for x in (A, B, old):
        x.stop()
sys.exit(rc)
