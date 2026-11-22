#!/usr/bin/env python3
"""Scenario 4: a 0.14 miner with the majority and a SegWit output spent with no witness.

Links: 0.14.1 <-> EndTimes A <-> EndTimes B. A "theft" block is built by hand: valid for
0.14.1, invalid under SegWit (it spends a P2WPKH output and a P2SH-P2WPKH output with no
witness). It is given to 0.14.1 only, and 0.14.1 extends it so that chain has the most work.

  part 0 (before activation, blocks H-9 and H-7): the same kind of block, once with a bare
         P2WPKH output and once with a P2SH-wrapped one. EndTimes enforces the witness
         SCRIPT rule on every block once a SegWit height is configured at all
         (validation.cpp GetBlockScriptFlags), 0.14.1 does not, so the two can already
         disagree BEFORE the fork height. The check "before activation EndTimes and 0.14.1
         AGREE" is expected to hold on a safe design; if it fails that is a finding.
  part 1 (after activation): EndTimes nodes must refuse the theft chain and stay put;
         0.14.1 is recorded following it; then the valid chain overtakes and 0.14.1 must
         reorg back; the rightful owner then spends the outputs with witnesses.
  part 2: what STOCK 0.14.1 does with such a spend on regtest (mempool, template, own
         block). Regtest switches standardness off in 0.14.1, so this part only reports;
         the mainnet-rules answer is sw2m_s4b_mainnet_policy.py.

Chain state on 0.14.1 is read with gettxout(txid, n, false): the default also looks at the
mempool, and after a reorg 0.14.1 puts the disconnected block's transactions back there.

--mutant no-segwit : EndTimes nodes run with SegWit and CSV switched off (-segwitheight=-1)
                     -> the "EndTimes stays on the valid chain" checks must FAIL
"""
import sys, time
from sw2m import *  # noqa: F401,F403

ap = std_args(__doc__)
ap.add_argument("--mutant", choices=["no-segwit"])
a = ap.parse_args()
if a.mode != "old":
    raise SystemExit("scenario 4 needs the 0.14.1 daemon: --mode old")
H, c = a.height, Checks()
net = Net(a.work, a.newbin, a.ref, a.mode, H, a.port0)
hk = {"segwit": -1, "csv": -1} if a.mutant else {}
rc = 1


def old_mines(old, n=1):
    h0 = old.height()
    old.setgenerate(True, n)
    if not wait_until(lambda: old.height() >= h0 + n, 120):
        raise SystemExit("0.14.1 did not mine")


try:
    old = net.old()
    A, B = net.new("A", **hk), net.new("B", **hk)
    A.call("createwallet", "w"); A.wallet = "w"
    connect(A, old); connect(A, B)
    trio = [A, B, old]
    say("== scenario 4: SegWit/CSV/CLTV at %d%s" % (H, "  MUTANT " + a.mutant if a.mutant else ""))
    la = A.getnewaddress("", "legacy")
    A.generatetoaddress(H - 14, la)
    ka = A.getnewaddress("", "legacy")
    kpriv, kpub = A.dumpprivkey(ka), A.getaddressinfo(ka)["pubkey"]
    W, KL = p2wpkh_script(kpub), bytes.fromhex(A.getaddressinfo(ka)["scriptPubKey"])
    WS = p2sh_of(W)
    coin = Coins(A).take(100)
    v = sat(coin["amount"]) // 14
    outs = [(W, v)] * 6 + [(WS, v)] * 5        # outputs 0-5 bare P2WPKH, 6-10 P2SH-P2WPKH
    fund = sign_wallet(A, mktx([(coin["txid"], coin["vout"], 0xffffffff)],
                               outs + [(bytes.fromhex(coin["scriptPubKey"]), sat(coin["amount"]) - v * len(outs) - 100000)]))
    A.sendrawtransaction(fund.serialize().hex())
    A.lockunspent(False, [{"txid": fund.hash, "vout": i} for i in range(len(outs))])
    F = fund.hash
    A.generatetoaddress(2, la)
    c.ok(same_tip(trio, 300) and A.height() == H - 12, "all three at H-12, victim outputs confirmed", tips(trio))
    thief = bytes.fromhex(A.validateaddress(old.getnewaddress())["scriptPubKey"])   # a 0.14.1 wallet address

    def steal(i, wrapped=False):
        """Spend funding output i with NO witness (and, if wrapped, just the redeem script push)."""
        t = mktx([(F, i, 0xffffffff)], [(thief, v - 50000)])
        if wrapped:
            t.vin[0].scriptSig = bytes(CScript([bytes(W)]))
            t.rehash()
        return t

    def owner_spend(i, wrapped=False):
        t = mktx([(F, i, 0xffffffff)], [(KL, v - 50000)])
        return sign_key(A, t, [kpriv], [(F, i, WS if wrapped else W, v, W if wrapped else None)])

    def theft_block(txs):
        """Valid for 0.14.1: chain ID set, base version 3, no witness commitment, no witness data."""
        return build_block(template(old), txs, chain_id=BOB_CHAIN_ID, base=3, commit="none")

    def unspent_in_chain(i):
        return old.gettxout(F, i, False) is not None

    # ---------------- part 0: before activation ----------------
    pre = []
    for name, t in (("P2WPKH", steal(0)), ("P2SH-P2WPKH", steal(10, wrapped=True))):
        say("== part 0, BEFORE activation (block %d = H%+d): a block spending a %s output with no witness" % (A.height() + 1, A.height() + 1 - H, name))
        good0 = A.tip()
        x0 = theft_block([t])
        res = submit(old, x0, witness=False)
        c.ok(res is None and old.tip() == x0.hash, "0.14.1 accepts it as its tip (to 0.14.1 the %s output is anyone-can-spend)" % name, res)
        time.sleep(8)       # before activation A does fetch 0.14.1's blocks over P2P
        verdict = submit(A, x0, witness=False)
        say("   EndTimes A: tip %s, submitblock -> %s" % ("UNCHANGED" if A.tip() == good0 else "moved to " + A.tip()[:12], verdict))
        for l in A.grep(x0.hash)[-3:]:
            say("        A log: " + l[:220])
        agree = A.tip() == old.tip()
        c.ok(agree, "before activation EndTimes and 0.14.1 AGREE on a block with a witness-less %s spend (same tip)" % name,
             "" if agree else "SPLIT before the fork height: 0.14.1 on %s (h=%d), EndTimes on %s (h=%d), A says: %s" %
             (old.tip()[:12], old.height(), A.tip()[:12], A.height(), verdict))
        if not agree:
            A.generatetoaddress(2, la)
            c.ok(wait_until(lambda: old.tip() == A.tip(), 60), "   EndTimes mines 2 blocks; 0.14.1 reorgs back onto the EndTimes chain", tips(trio))
            i = 10 if "P2SH" in name else 0
            c.ok(unspent_in_chain(i), "   in 0.14.1's chain the output is unspent again")
            held = t.hash in old.getrawmempool()
            say("   NOTE after that reorg regtest 0.14.1 has put the theft tx back in its MEMPOOL: %s" % held)
            pre.append((name, t, i))
    A.generatetoaddress(H + 3 - A.height(), la)
    c.ok(same_tip(trio, 60) and A.height() == H + 3, "all three at H+3 (past activation)", tips(trio))

    # ---------------- part 1: after activation ----------------
    say("== part 1, AFTER activation: theft block at H+4, given to 0.14.1 only")
    for name, t in (("P2WPKH", steal(1)), ("P2SH-P2WPKH", steal(6, wrapped=True))):
        e = A.err("sendrawtransaction", legacy_hex(t))
        c.ok(e != "", "EndTimes mempool refuses a witness-less %s spend" % name, e)
    good = A.tip()
    x1 = theft_block([steal(1), steal(6, wrapped=True)])
    res = submit(old, x1, witness=False)
    c.ok(res is None and old.tip() == x1.hash, "0.14.1 ACCEPTS the theft block as its tip (premise of the attack)", res)
    old_mines(old, 3)
    c.ok(old.height() == H + 7 and old.getblockhash(H + 4) == x1.hash, "0.14.1 extends it by 3: theft chain H+7, valid chain H+3")
    time.sleep(10)
    c.ok(A.tip() == good and B.tip() == good, "EndTimes A and B stay on the valid chain (10 s later)", tips(trio))
    say("   A's view of the theft chain before anyone hands it the blocks: " +
        "; ".join("%d %s %s" % (t["height"], t["hash"][:12], t["status"]) for t in A.getchaintips() if t["hash"] != good))
    rejects(c, [A, B], x1, "Witness program hash mismatch", "the theft block when handed it directly", witness=False)
    for l in A.grep(x1.hash)[-2:]:
        say("        A log: " + l[:220])
    for h in range(H + 5, H + 8):
        res = A.err("submitblock", old.getblock(old.getblockhash(h), False)) or A.submitblock(old.getblock(old.getblockhash(h), False))
        say("   A, 0.14.1's descendant block %d -> %s" % (h, res))
    c.ok(A.tip() == good and B.tip() == good, "EndTimes still on the valid chain after seeing all 4 theft-chain blocks", tips(trio))
    c.ok(any(t["status"] == "invalid" and t["height"] >= H + 4 for t in A.getchaintips()), "A marks the whole theft chain invalid",
         [(t["height"], t["status"]) for t in A.getchaintips()])
    c.ok(not unspent_in_chain(1) and not unspent_in_chain(6) and old.height() == H + 7,
         "(recorded) 0.14.1 FOLLOWS the theft chain: both outputs are spent in its view, tip H+7")

    say("== the valid chain grows but is still behind")
    t_own = owner_spend(2)
    A.sendrawtransaction(t_own.serialize().hex())
    A.generatetoaddress(2, la)
    time.sleep(5)
    c.ok(A.height() == H + 5 and old.height() == H + 7 and old.getblockhash(H + 4) == x1.hash,
         "(recorded) valid chain H+5 < theft chain H+7: 0.14.1 stays on the theft chain", tips(trio))
    say("== the valid chain overtakes")
    A.generatetoaddress(3, la)
    c.ok(wait_until(lambda: old.tip() == A.tip(), 90), "0.14.1 REORGS back onto the valid chain (H+8)", tips(trio))
    c.ok(unspent_in_chain(1) and unspent_in_chain(6), "in 0.14.1's chain the two outputs are unspent again")
    om = old.getrawmempool()
    back = [n for n, t in (("P2WPKH theft", steal(1)), ("P2SH-P2WPKH theft", steal(6, wrapped=True))) if t.hash in om]
    say("   NOTE regtest 0.14.1 mempool after the reorg holds %d tx; theft txs put back by 0.14.1: %s (it would mine them again: part 2)" % (len(om), back or "none"))

    say("== the owner spends both outputs properly")
    own = [owner_spend(1), owner_spend(6, wrapped=True)] + [owner_spend(i, wrapped=(i >= 6)) for _, _, i in pre]
    for t in own:
        A.sendrawtransaction(t.serialize().hex())
    A.generatetoaddress(1, la)
    c.ok({t.hash for t in own} <= set(block_txids(A, A.tip())), "owner's %d witness spends confirmed by EndTimes" % len(own))
    c.ok(wait_until(lambda: all(n.tip() == A.tip() for n in trio), 60), "all three on it", tips(trio))
    c.ok(wait_until(lambda: old.getrawmempool() == [], 20), "0.14.1's mempool drops every theft tx once the owner's spends are confirmed", old.getrawmempool())

    # ---------------- part 2: stock 0.14.1 on regtest (report only) ----------------
    say("== part 2 (REPORT ONLY, regtest policy): does STOCK 0.14.1 take such spends by itself?")
    notes = []
    for name, t in (("P2WPKH", steal(3)), ("P2SH-P2WPKH", steal(7, wrapped=True))):
        e = old.err("sendrawtransaction", legacy_hex(t))
        inpool = t.hash in old.getrawmempool()
        notes.append((name, t, inpool))
        say("   NOTE regtest 0.14.1 sendrawtransaction witness-less %s spend -> %s; in its mempool: %s" % (name, e or "ACCEPTED", inpool))
    time.sleep(3)
    say("   NOTE 0.14.1 relayed them to EndTimes A's mempool: %s" % [n for n, t, _ in notes if t.hash in A.getrawmempool()])
    tplt = [x["hash"] for x in old.getblocktemplate()["transactions"]]
    say("   NOTE regtest 0.14.1 getblocktemplate includes: %s" % ([n for n, t, _ in notes if t.hash in tplt] or "neither"))
    if any(i for _, _, i in notes):
        g = A.tip()
        old_mines(old, 1)
        ob = old.tip()
        inblk = [n for n, t, _ in notes if t.hash in old.getblock(ob)["tx"]]
        res = A.err("submitblock", old.getblock(ob, False)) or A.submitblock(old.getblock(ob, False))
        say("   NOTE regtest 0.14.1 then MINED a block containing %s; EndTimes says: %s" % (inblk, res))
        c.ok(A.tip() == g, "EndTimes refuses stock 0.14.1's own block when it contains a witness-less spend", res)
        A.generatetoaddress(2, la)
        c.ok(wait_until(lambda: old.tip() == A.tip(), 60), "0.14.1 reorgs back once the valid chain is longer", tips(trio))
    n_ = A.height()
    c.ok(same_tip(trio, 60) and all(len({x.getblockhash(h) for x in trio}) == 1 for h in range(n_ + 1)), "end state: chains identical 0..%d" % n_, tips(trio))
    rc = c.finish("scenario 4" + (" (mutant %s)" % a.mutant if a.mutant else ""))
finally:
    net.stop()
    if not a.keep and rc == 0:
        shutil.rmtree(a.work, ignore_errors=True)
sys.exit(rc)
