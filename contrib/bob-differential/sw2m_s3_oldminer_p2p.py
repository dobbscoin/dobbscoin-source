#!/usr/bin/env python3
"""Scenario 3: blocks from a 0.14.1 MINER after activation, over P2P ONLY.

Chain of links:  0.14.1  <->  EndTimes A  <->  EndTimes B   (B has no link to 0.14.1).
No submitblock between them until the very end. Measures, with A's -debug=net log as
evidence, whether and how a block mined by 0.14.1 reaches A and B:
  control : before activation (must arrive, proves the link and the method work)
  after   : after activation, link opened by A (outbound), then link opened by 0.14.1
            (inbound at A), then several 0.14.1 blocks in a row
  split   : A mines on while 0.14.1 is ahead: who follows whom
  valid?  : the same 0.14.1 block handed to A with submitblock (is the block itself fine,
            and does A then relay it to B)

The check that fails if EndTimes never fetches the block is "0.14.1's post-activation
block reaches EndTimes A over P2P". Only --mode old makes sense here.
"""
import sys, time
from sw2m import *  # noqa: F401,F403

ap = std_args(__doc__)
ap.add_argument("--wait", type=int, default=120, help="seconds to wait for a block to arrive over P2P")
a = ap.parse_args()
if a.mode != "old":
    raise SystemExit("scenario 3 needs the 0.14.1 daemon: --mode old")
H, c = a.height, Checks()
net = Net(a.work, a.newbin, a.ref, a.mode, H, a.port0)
rc = 1


def old_mines(old, n=1):
    h0 = old.height()
    old.setgenerate(True, n)
    if not wait_until(lambda: old.height() >= h0 + n, 120):
        raise SystemExit("0.14.1 did not mine")
    return [old.getblockhash(h) for h in range(h0 + 1, h0 + n + 1)]


def arrival(node, bh, timeout):
    t0 = time.time()
    got = wait_until(lambda: node.tip() == bh, timeout, 0.1)
    return got, time.time() - t0


def tipstatus(node, bh):
    for t in node.getchaintips():
        if t["hash"] == bh:
            return "%s (height %d, branchlen %d)" % (t["status"], t["height"], t["branchlen"])
    try:
        hd = node.getblockheader(bh)
        return "header known, confirmations %s" % hd["confirmations"]
    except RpcError:
        return "header UNKNOWN"


def evidence(node, since, bh, peer_hint=""):
    keys = ("received: inv", "sending getheaders", "getheaders", "received: headers", "sending getdata", "received: block",
            "Requesting block", bh[:16], "received: cmpctblock")
    seen, out = set(), []
    for l in node.grep("", since):
        if any(k in l for k in keys) and "sending inv" not in l:
            k = l[21:90]
            if k not in seen:
                seen.add(k); out.append(l)
    for l in out[:14]:
        say("        %s log: %s" % (node.name, l[:200]))
    return out


try:
    old = net.old()
    A, B = net.new("A"), net.new("B")
    A.call("createwallet", "w"); A.wallet = "w"
    connect(A, old); connect(B, A)
    say("== scenario 3: 0.14.1 <-> A <-> B, SegWit/CSV/CLTV at %d" % H)
    la = A.getnewaddress("", "legacy")
    A.generatetoaddress(H - 5, la)
    c.ok(same_tip([A, B, old], 300), "all three at H-5", tips([A, B, old]))
    pi = [p for p in A.getpeerinfo() if p["addr"] == old.addr][0]
    say("   A's view of the 0.14.1 peer: version %s, subver %s, services %s (%s), inbound=%s" %
        (pi["version"], pi["subver"], pi["services"], ",".join(pi.get("servicesnames", [])), pi["inbound"]))
    c.ok("WITNESS" not in ",".join(pi.get("servicesnames", [])), "0.14.1 does not offer NODE_WITNESS")

    say("== control, BEFORE activation: 0.14.1 mines block H-4")
    pos = A.logpos()
    (bh,) = old_mines(old)
    got, dt = arrival(A, bh, 60)
    c.ok(got, "pre-activation: A gets 0.14.1's block over P2P", "%.2fs" % dt)
    gotb, dtb = arrival(B, bh, 60)
    c.ok(gotb, "pre-activation: B gets it through A", "%.2fs after A" % dtb)
    evidence(A, pos, bh)

    A.generatetoaddress(H + 2 - A.height(), la)
    c.ok(same_tip([A, B, old], 60) and A.height() == H + 2, "all three at H+2 (EndTimes blocks; 0.14.1 follows)", tips([A, B, old]))

    say("== AFTER activation: 0.14.1 mines block H+3; link was opened by A (outbound, manual)")
    pos = A.logpos()
    (o1,) = old_mines(old)
    got, dt = arrival(A, o1, a.wait)
    delivered = c.ok(got, "0.14.1's post-activation block reaches EndTimes A over P2P (waited %ds)" % a.wait,
                     "%.2fs" % dt if got else "NEVER; A tip %d, A's view of the block: %s" % (A.height(), tipstatus(A, o1)))
    ev = evidence(A, pos, o1)
    asked = any("getdata" in l and "sending" in l for l in ev)
    say("   A sent a getdata for it: %s; A's view: %s; B's view: %s" % (asked, tipstatus(A, o1), tipstatus(B, o1)))
    gotb, _ = arrival(B, o1, 5)
    c.ok(gotb, "...and B (reachable only through A) has it", "B tip %d, view: %s" % (B.height(), tipstatus(B, o1)))

    if not delivered:
        say("== same, link opened by 0.14.1 instead (inbound at A)")
        disconnect(A, old)
        old.addnode(A.addr, "onetry")
        c.ok(wait_until(lambda: any(p["inbound"] and "0.14" in p["subver"] for p in A.getpeerinfo()), 30), "0.14.1 connected in to A")
        pos = A.logpos()
        (o2,) = old_mines(old)
        got, dt = arrival(A, o2, a.wait)
        c.ok(got, "inbound link: 0.14.1's next block (H+4) reaches A over P2P (waited %ds)" % a.wait,
             "%.2fs" % dt if got else "NEVER; A tip %d, A's view: %s" % (A.height(), tipstatus(A, o2)))
        evidence(A, pos, o2)
        o_more = old_mines(old, 2)
        got, dt = arrival(A, o_more[-1], 30)
        c.ok(got, "two more 0.14.1 blocks (H+5, H+6, 4 ahead of A): A catches up over P2P",
             "%.2fs" % dt if got else "NEVER; old at %d, A at %d, A's view of old's tip: %s" % (old.height(), A.height(), tipstatus(A, o_more[-1])))
        say("   chaintips on A: " + "; ".join("%d %s %s" % (t["height"], t["hash"][:12], t["status"]) for t in A.getchaintips()))

        say("== the split while it lasts: A mines its own H+3 (it has never seen 0.14.1's)")
        A.generatetoaddress(1, la)
        say("   " + tips([A, B, old]))
        c.ok(A.getblockhash(H + 3) != old.getblockhash(H + 3), "(observation) A and 0.14.1 hold DIFFERENT blocks at H+3: a live fork",
             "A %s vs 0.14.1 %s" % (A.getblockhash(H + 3)[:12], old.getblockhash(H + 3)[:12]))

        say("== is the 0.14.1 block itself acceptable? hand 0.14.1's chain to A with submitblock")
        otip = old.tip()
        for h in range(H + 3, old.height() + 1):
            res = A.submitblock(old.getblock(old.getblockhash(h), False))
            say("   submitblock 0.14.1 block %d -> %s" % (h, res))
        c.ok(A.tip() == otip, "by submitblock A accepts 0.14.1's post-activation blocks and reorgs onto them", tips([A, old]))
        gotb, dtb = arrival(B, otip, 60)
        c.ok(gotb, "A then relays them to B over P2P", "%.2fs" % dtb)

        say("== and when EndTimes miners are ahead: 0.14.1's unseen blocks are simply orphaned")
        (o9,) = old_mines(old)                      # 0.14.1 extends by one that A will not fetch
        c.ok(not arrival(A, o9, 10)[0], "(observation) again not fetched", tipstatus(A, o9))
        A.generatetoaddress(2, la)                  # EndTimes side now has more work
        c.ok(wait_until(lambda: old.tip() == A.tip(), 60), "0.14.1 reorgs onto the EndTimes chain; its own block is orphaned", tips([A, old]))
        c.ok(old.getblock(o9)["confirmations"] < 0, "   0.14.1's block %s now has confirmations -1 on 0.14.1 itself" % o9[:12])
    n = A.height()
    c.ok(same_tip([A, B, old], 60) and all(len({x.getblockhash(h) for x in (A, B, old)}) == 1 for h in range(n + 1)),
         "end state: chains identical 0..%d" % n, tips([A, B, old]))
    rc = c.finish("scenario 3")
finally:
    net.stop()
    if not a.keep and rc == 0:
        shutil.rmtree(a.work, ignore_errors=True)
sys.exit(rc)
