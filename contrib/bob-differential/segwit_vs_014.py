#!/usr/bin/env python3
"""(BOB) SegWit/CSV activation rehearsal against the real 0.14.1 daemon (regtest).

A soft fork: nodes that do not upgrade must keep following the chain. 0.14.1 (no
SegWit, no CSV) and EndTimes (activated at ACT by height) share one regtest over
P2P, so 0.14.1 receives EndTimes' blocks the way it would in real life, with the
witness data stripped. Checks:
  1. 0.14.1 follows every block EndTimes mines up to, at and after activation,
     including blocks with bech32 payments, witness spends and BIP68 (v2) spends.
  2. EndTimes accepts blocks 0.14.1 mines after activation.
  3. Both chains are identical at the end.
usage: segwit_vs_014.py <endtimes-src-dir> <ref-0.14.1-dir> <workdir>
"""
import json, os, subprocess, sys, time, urllib.request
from decimal import Decimal

NEW, REF, WORK = (os.path.abspath(a) for a in sys.argv[1:4])
ACT = 200  # SegWit + CSV activation height (regtest: AuxPoW at 10, CLTV at 150)
checks, problems = 0, []


class Node:
    def __init__(self, name, argv, port, env=None):
        self.name, self.port = name, port
        dd = os.path.join(WORK, name); os.makedirs(dd, exist_ok=True)
        self.proc = subprocess.Popen(argv + ["-regtest", "-datadir=" + dd, "-rpcuser=d", "-rpcpassword=d",
                                             "-rpcport=%d" % port, "-server=1", "-printtoconsole=0"],
                                     stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=env)
        for _ in range(120):
            try:
                self.call("getblockcount"); return
            except Exception:
                time.sleep(0.5)
        raise SystemExit("%s did not start" % name)

    def call(self, method, *params, wallet=None):
        url = "http://127.0.0.1:%d/%s" % (self.port, "wallet/" + wallet if wallet else "")
        req = urllib.request.Request(url, data=json.dumps({"id": 1, "method": method, "params": list(params)}).encode(),
                                     headers={"Authorization": "Basic ZDpk", "Content-Type": "application/json"})
        try:
            r = json.loads(urllib.request.urlopen(req, timeout=120).read(), parse_float=Decimal)
        except urllib.error.HTTPError as e:
            r = json.loads(e.read(), parse_float=Decimal)
        if r.get("error"):
            raise RuntimeError("%s: %s" % (method, r["error"]))
        return r["result"]

    def stop(self):
        try: self.call("stop")
        except Exception: pass
        try: self.proc.wait(30)
        except Exception: self.proc.kill()


def follow(label, timeout=60):
    """0.14.1 must reach EndTimes' tip; record a check."""
    global checks
    want, h = new.call("getbestblockhash"), new.call("getblockcount")
    t0 = time.time()
    while time.time() - t0 < timeout:
        if old.call("getblockcount") >= h and old.call("getblockhash", h) == want:
            checks += 1; print("   ok  h=%-4d %s" % (h, label), flush=True); return
        time.sleep(0.5)
    problems.append((h, label)); print("   !!  h=%-4d %s: 0.14.1 at %d did not follow" % (h, label, old.call("getblockcount")), flush=True)


def mine_new(n, label):
    for _ in range(n):
        new.call("generatetoaddress", 1, nl, wallet="w"); follow(label)


env = dict(os.environ, LD_LIBRARY_PATH=os.path.join(REF, "lib"))
old = Node("old-0.14.1", [os.path.join(REF, "dobbscoind"), "-port=19656", "-listen=1", "-bind=127.0.0.1"], 19655, env)
new = Node("endtimes", [os.path.join(NEW, "dobbscoind"), "-port=19666", "-listen=0", "-connect=127.0.0.1:19656",
                        "-segwitheight=%d" % ACT, "-csvheight=%d" % ACT], 19665)
try:
    for _ in range(60):
        if new.call("getconnectioncount") > 0: break
        time.sleep(0.5)
    new.call("createwallet", "w")
    nl = new.call("getnewaddress", "", "legacy", wallet="w")
    print("== up to activation (%d)" % ACT)
    new.call("generatetoaddress", ACT - 3, nl, wallet="w"); follow("EndTimes blocks 1..%d" % (ACT - 3), 300)
    mine_new(1, "last pre-activation blocks")
    mine_new(1, "last pre-activation blocks")
    assert new.call("getblockcount") == ACT - 1
    mine_new(1, "the activation block %d" % ACT)

    print("== after activation: SegWit and CSV transactions in EndTimes blocks")
    bech = new.call("getnewaddress", "", "bech32", wallet="w")
    p2sh = new.call("getnewaddress", "", "p2sh-segwit", wallet="w")
    for i in range(3):
        new.call("sendtoaddress", bech, 5 + i, wallet="w"); new.call("sendtoaddress", p2sh, 2 + i, wallet="w")
    mine_new(1, "block paying bech32 and p2sh-segwit outputs")
    for i in range(3):
        new.call("sendtoaddress", new.call("getnewaddress", "", "bech32", wallet="w"), 4, wallet="w")
    mine_new(1, "block with witness spends (stripped for 0.14.1)")
    blk = new.call("getblock", new.call("getbestblockhash"), 2)
    wit = sum(1 for tx in blk["tx"][1:] for vin in tx["vin"] if vin.get("txinwitness"))
    print("   (that block: %d txs, %d witness inputs, weight %s)" % (len(blk["tx"]), wit, blk["weight"]))
    if wit == 0: problems.append((blk["height"], "no witness inputs in the witness-spend block"))
    # BIP68 v2 spend whose relative lock is already satisfied
    u = [x for x in new.call("listunspent", 6, 9999999, wallet="w") if x["address"] == nl][0]
    raw = new.call("createrawtransaction", [{"txid": u["txid"], "vout": u["vout"], "sequence": 5}],
                   {new.call("getnewaddress", "", "legacy", wallet="w"): float(u["amount"] - Decimal("0.01"))}, 0, False)
    raw = "02" + raw[2:]  # version 2
    signed = new.call("signrawtransactionwithwallet", raw, wallet="w")["hex"]
    new.call("sendrawtransaction", signed)
    mine_new(1, "block with a BIP68 (v2, relative-lock) spend")
    mine_new(5, "plain EndTimes blocks after activation")

    print("== 0.14.1 mines after activation; its blocks must be VALID to EndTimes")
    # After activation EndTimes does not download blocks from peers without NODE_WITNESS
    # (BIP144), so a 0.14 miner's block only reaches it through an upgraded relay; hand
    # it over with submitblock, as such a relay would.
    for i in range(3):
        h0 = new.call("getblockcount")
        old.call("setgenerate", True, 1)
        for _ in range(240):
            if old.call("getblockcount") > h0: break
            time.sleep(0.5)
        bh = old.call("getbestblockhash")
        res = new.call("submitblock", old.call("getblock", bh, False))
        acc = new.call("getbestblockhash") == bh
        checks += 1
        print("   %s  h=%-4d 0.14.1 block valid for EndTimes (submitblock: %s)" % ("ok" if acc else "!!", h0 + 1, res), flush=True)
        if not acc: problems.append((h0 + 1, "EndTimes rejected a 0.14.1 block: %s" % res))
    mine_new(2, "EndTimes builds on the 0.14.1 blocks")

    n = new.call("getblockcount")
    same = all(old.call("getblockhash", h) == new.call("getblockhash", h) for h in range(n + 1))
    checks += 1
    if not same: problems.append((n, "chains differ"))
    print("== chains %s through %d" % ("identical" if same else "DIFFER", n))
    print("\n%d checks, %d problem(s)%s" % (checks, len(problems), "".join("\n  PROBLEM %s" % (p,) for p in problems)))
    rc = 0 if not problems else 1
finally:
    old.stop(); new.stop()
sys.exit(rc)
