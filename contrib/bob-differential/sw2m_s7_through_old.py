#!/usr/bin/env python3
"""Scenario 7: an upgraded node whose ONLY peer is a 0.14.1 node.

Links:  EndTimes A (miner)  <->  0.14.1  <->  EndTimes B.   A and B have no direct link.
After activation every block A mines carries a witness commitment, and 0.14.1 can only
pass blocks on without witness data.
  plain  : A's blocks WITHOUT witness spends must reach B through 0.14.1 (B puts the
           all-zero coinbase witness back and checks it against the commitment).
  witness: a block WITH a witness spend cannot be completed that way. B must not accept
           it, must not punish 0.14.1, must not mark anything invalid, and must wait.
  heal   : once B gets a link to A it catches up to the same tip.
Only --mode old makes sense here.
"""
import sys, time
from sw2m import *  # noqa: F401,F403

ap = std_args(__doc__)
a = ap.parse_args()
if a.mode != "old":
    raise SystemExit("scenario 7 needs the 0.14.1 daemon: --mode old")
H, c = a.height, Checks()
net = Net(a.work, a.newbin, a.ref, a.mode, H, a.port0)
rc = 1
try:
    old = net.old()
    A, B = net.new("A"), net.new("B")
    A.call("createwallet", "w"); A.wallet = "w"
    connect(A, old); connect(B, old)
    say("== scenario 7: A <-> 0.14.1 <-> B, SegWit/CSV/CLTV at %d" % H)
    la = A.getnewaddress("", "legacy")
    A.generatetoaddress(H - 5, la)
    c.ok(same_tip([A, old, B], 300), "all three at H-5", tips([A, old, B]))
    c.ok(not linked(A, B), "A and B have no direct link")

    say("== plain blocks across activation: H-4 .. H+3, mined by A")
    A.generatetoaddress(8, la)
    c.ok(same_tip([A, old, B], 60) and B.height() == H + 3, "B follows A's post-activation blocks through 0.14.1", tips([A, old, B]))
    present, correct, wins, _, _ = commitment_check(A, A.tip())
    c.ok(present and correct and wins == 0, "those blocks carry a witness commitment and no witness inputs")
    c.ok(B.getblock(B.tip(), 0) == A.getblock(A.tip(), 0), "B stores the block byte-identical to A's (coinbase witness restored)")

    say("== a payment TO a bech32 address is still a plain block")
    wa = A.getnewaddress("", "bech32")
    pay = A.sendtoaddress(wa, 5)
    A.generatetoaddress(1, la)
    c.ok(same_tip([A, old, B], 60), "B gets the block holding the payment to bech32", tips([A, old, B]))
    parent = A.tip()

    say("== a block WITH a witness spend")
    u = [x for x in A.listunspent(1) if x["txid"] == pay and x["address"] == wa][0]
    raw = A.createrawtransaction([{"txid": u["txid"], "vout": u["vout"]}], {la: 4.99})
    sig = A.signrawtransactionwithwallet(raw)["hex"]
    posB = B.logpos()
    wtx = A.sendrawtransaction(sig)
    time.sleep(3)
    c.ok(wtx not in old.getrawmempool(), "A does not hand the witness spend to 0.14.1 (it would arrive stripped)")
    A.generatetoaddress(1, la)
    wblock = A.tip()
    c.ok(wtx in block_txids(A, wblock), "A mined the witness spend into block %d" % A.height())
    c.ok(wait_until(lambda: old.tip() == wblock, 60), "0.14.1 follows that block", tips([A, old]))
    time.sleep(15)
    c.ok(B.tip() == parent, "B does NOT take the stripped block from 0.14.1 and stays on its parent", tips([A, old, B]))
    c.ok(any("needs witness data" in l for l in B.grep("needs witness data", posB)), "B logged that the block needs a witness peer")
    c.ok(not any(t["status"] == "invalid" for t in B.getchaintips()), "B marked nothing invalid", str(B.getchaintips())[:200])
    c.ok(linked(B, old) and not B.listbanned(), "B still has its 0.14.1 peer and banned nobody")
    A.generatetoaddress(2, la)
    time.sleep(15)
    c.ok(B.tip() == parent, "two more blocks on top: B is still waiting (it has no witness peer)", tips([A, old, B]))

    say("== heal: B gets a link to A")
    connect(B, A)
    c.ok(same_tip([A, old, B], 60), "B catches up to A's tip", tips([A, old, B]))
    n = A.height()
    c.ok(all(len({x.getblockhash(h) for x in (A, B, old)}) == 1 for h in range(n + 1)), "end state: chains identical 0..%d" % n)
    rc = c.finish("scenario 7")
finally:
    net.stop()
    if not a.keep and rc == 0:
        shutil.rmtree(a.work, ignore_errors=True)
sys.exit(rc)
