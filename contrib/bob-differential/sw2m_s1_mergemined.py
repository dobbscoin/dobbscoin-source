#!/usr/bin/env python3
"""Scenario 1: all four rules at one height, blocks produced by MERGE MINING.

From height H every block here is made through createauxblock/submitauxblock. Block H
itself (the first AuxPoW block, the first SegWit block, the first CLTV/CSV block) must
carry a witness spend, a v2 BIP68 spend and a CLTV-locked spend. Later blocks carry a
wallet bech32 payment, its witness spend, a P2SH-wrapped witness spend and a BIP68 spend
of a witness output. Every block's witness commitment is recomputed from the raw block,
a second EndTimes node must take each block over P2P WITH its witness data, and (mode
old) the real 0.14.1 must follow.

The reverse: hand-built AuxPoW blocks with a wrong commitment, no commitment, a stripped
witness, or a BIP68 violation must be rejected, and the same builder's correct block
accepted (the control that shows the rejections are about the defect and nothing else).

--mutant corrupt-control : the control block gets a corrupted commitment  -> must FAIL
--mutant b-segwit-late   : node B runs SegWit/CSV one block late (H+1)   -> must FAIL
"""
import sys
from sw2m import *  # noqa: F401,F403

ap = std_args(__doc__)
ap.add_argument("--mutant", choices=["corrupt-control", "b-segwit-late"])
a = ap.parse_args()
H, c = a.height, Checks()
net = Net(a.work, a.newbin, a.ref, a.mode, H, a.port0)
rc = 1
try:
    A = net.new("A")
    B = net.new("B", **({"segwit": H + 1, "csv": H + 1} if a.mutant == "b-segwit-late" else {}))
    old = net.old() if a.mode == "old" else None
    for n in (A, B):
        n.call("createwallet", "w"); n.wallet = "w"
    connect(A, B)
    if old:
        connect(A, old)
    everyone = [A, B] + ([old] if old else [])
    say("== scenario 1, mode %s: AuxPoW at %d, CLTV/SegWit/CSV at %d%s" % (a.mode, net.auxpow, H, "  MUTANT " + a.mutant if a.mutant else ""))

    la = A.getnewaddress("", "legacy")
    A.generatetoaddress(H - 3, la)
    c.ok(same_tip(everyone, 300), "everyone at H-3", tips(everyone))

    # ---- before activation: outputs for later, made by hand (the wallet refuses bech32 until SegWit is active)
    ka = A.getnewaddress("", "legacy")
    kpriv, kpub = A.dumpprivkey(ka), A.getaddressinfo(ka)["pubkey"]
    W = p2wpkh_script(kpub)
    WS = p2sh_of(W)
    KL = bytes.fromhex(A.getaddressinfo(ka)["scriptPubKey"])
    ck = new_key()
    cltv_redeem, CL = cltv_script(H - 1, ck)
    coin = Coins(A).take(100)
    v = sat(coin["amount"]) // 14
    outs = [(W, v)] * 6 + [(WS, v)] * 2 + [(KL, v)] * 3 + [(CL, v)]
    change = sat(coin["amount"]) - v * len(outs) - 100000
    fund = sign_wallet(A, mktx([(coin["txid"], coin["vout"], 0xffffffff)], outs + [(bytes.fromhex(coin["scriptPubKey"]), change)]))
    e = A.err("sendrawtransaction", fund.serialize().hex())
    c.ok(e == "", "pre-activation funding tx (pays witness programs, raw) enters the mempool", e)
    A.lockunspent(False, [{"txid": fund.hash, "vout": i} for i in range(len(outs))])   # ours to spend by hand, not the wallet's
    c.ok("until SegWit is activated" in A.err("getnewaddress", "", "bech32"), "wallet refuses a bech32 address 3 blocks before H")
    if a.mode == "all4":
        c.ok("not yet reached" in A.err("createauxblock", la), "createauxblock refuses at tip H-3")
    A.generatetoaddress(1, la)
    F = fund.hash
    c.ok(F in block_txids(A, A.tip()), "funding tx confirmed in block H-2")
    if a.mode == "all4":
        c.ok("not yet reached" in A.err("createauxblock", la), "createauxblock refuses at tip H-2 (next block H-1)")
    A.generatetoaddress(1, la)
    c.ok(same_tip(everyone, 60) and A.height() == H - 1, "everyone at H-1", tips(everyone))

    def wspend(i, seq=0xffffffff, version=1, wrapped=False):
        """Spend output i of the funding tx (P2WPKH, or P2SH-P2WPKH) with a witness, to a legacy output."""
        t = mktx([(F, i, seq)], [(KL, v - 50000)], version)
        return sign_key(A, t, [kpriv], [(F, i, WS if wrapped else W, v, W if wrapped else None)])

    # ---- block H: first AuxPoW + SegWit + CSV + CLTV block, merge-mined, with one of each in it
    say("== block H=%d through createauxblock/submitauxblock" % H)
    t_wit = wspend(0)
    c.ok(has_witness(t_wit), "the P2WPKH spend carries a witness")
    t_csv = sign_key(A, mktx([(F, 8, 2)], [(KL, v - 50000)], version=2), [kpriv], [(F, 8, KL, v, None)])
    t_cltv = sign_cltv(mktx([(F, 11, 0xfffffffe)], [(KL, v - 50000)], locktime=H - 1), 0, cltv_redeem, ck)
    for name, t in (("witness spend", t_wit), ("v2 BIP68 spend (relative lock 2, input 2 deep at H)", t_csv),
                    ("CLTV-locked spend (lock H-1)", t_cltv)):
        e = A.err("sendrawtransaction", t.serialize().hex())
        c.ok(e == "", "at tip H-1 the mempool takes a %s for block H" % name, e)
    hH = mm_mine(A, la, c, "block H merge-mined (submitauxblock true, new tip)")
    blk = A.getblock(hH)
    c.ok(blk["height"] == H, "it is block H", blk["height"])
    c.ok(all(t.hash in blk["tx"] for t in (t_wit, t_csv, t_cltv)), "block H contains the witness, BIP68 and CLTV spends",
         "%d txs" % len(blk["tx"]))

    def check_block(bh, label, want_wit=1):
        present, correct, wit, aux, chain = commitment_check(A, bh)
        c.ok(aux and chain == BOB_CHAIN_ID, "%s: AuxPoW bit set, chain ID 0x%04x" % (label, chain))
        c.ok(present and correct, "%s: coinbase witness commitment present and correct (recomputed)" % label,
             "present=%s correct=%s" % (present, correct))
        c.ok(wit >= want_wit, "%s: %d witness input(s) in the block" % (label, wit))
        got = wait_until(lambda: B.tip() == bh, 60)
        c.ok(got, "%s: second EndTimes node accepted it over P2P" % label, tips([A, B]))
        if got:
            pb, cb, wb, _, _ = commitment_check(B, bh)
            c.ok(pb and cb and wb == wit, "%s: B's copy has the witness data (%d witness inputs) and a correct commitment" % (label, wb))
        if old:
            if old_follows(c, old, A, label):
                c.ok(old.getblock(bh)["tx"] == A.getblock(bh)["tx"], "%s: 0.14.1 has the same txids in it" % label)

    check_block(hH, "block H")

    # ---- H+1: wallet bech32 + p2sh-segwit payments
    say("== later merge-mined blocks")
    bech = B.getnewaddress("", "bech32")
    c.ok(bech.startswith("dcrt1"), "after H the wallet hands out bech32", bech)
    pay = [A.sendtoaddress(bech, 7), A.sendtoaddress(B.getnewaddress("", "p2sh-segwit"), 6), A.sendtoaddress(B.getnewaddress("", "legacy"), 5)]
    h1 = mm_mine(A, la, c)
    c.ok(all(p in block_txids(A, h1) for p in pay), "block H+1 contains the bech32, p2sh-segwit and legacy payments")
    check_block(h1, "block H+1", 0)
    c.ok(wait_until(lambda: B.getbalances()["mine"]["trusted"] == 18, 30), "B's wallet sees 18 (BOB) received", B.getbalances()["mine"])

    # ---- H+2: B spends them with witnesses; plus raw: wrapped witness spend, BIP68 spend of a witness output
    back = B.sendtoaddress(A.getnewaddress("", "legacy"), 15)   # needs the bech32 AND the p2sh-segwit coin
    rawback = tx_from_hex(B.getrawtransaction(back))
    c.ok(sum(1 for w in rawback.wit.vtxinwit if w.scriptWitness.stack) >= 2, "B's wallet spend has witness inputs")
    c.ok(wait_until(lambda: back in A.getrawmempool(), 60), "B's witness spend relays to A's mempool")
    t_wrap = wspend(6, wrapped=True)
    t_wcsv = wspend(1, seq=3, version=2)          # funding tx is 4 deep at H+2
    for name, t in (("P2SH-P2WPKH spend", t_wrap), ("v2 BIP68 spend of a P2WPKH output", t_wcsv)):
        e = A.err("sendrawtransaction", t.serialize().hex())
        c.ok(e == "", "mempool takes a " + name, e)
    h2 = mm_mine(A, la, c)
    c.ok(all(t in block_txids(A, h2) for t in (back, t_wrap.hash, t_wcsv.hash)), "block H+2 contains all three")
    check_block(h2, "block H+2", 4)

    # ---- the reverse: defective AuxPoW blocks, built by hand, must be rejected
    say("== defective AuxPoW blocks at height %d" % (A.height() + 1))
    t_n = wspend(2)
    t_bad68 = wspend(3, seq=50, version=2)        # input is 5 deep, lock says 50
    tip0 = A.tip()

    def rejected(label, want, txs, **kw):
        b = build_block(template(A), txs, auxpow=True, **kw)
        rejects(c, [A, B], b, want, "an AuxPoW block with " + label)
        for l in A.grep(b.hash)[-1:]:
            say("        A debug.log: " + l[:230])
        c.ok(A.tip() == tip0, "tip unchanged")

    rejected("a WRONG witness commitment (witness tx inside)", "bad-witness-merkle-match", [t_n], commit="bad")
    rejected("NO witness commitment (witness tx inside)", "unexpected-witness", [t_n], commit="none")
    rejected("the witness STRIPPED from a P2WPKH spend", "Witness program hash mismatch", [strip_witness(t_n)], commit="good")
    rejected("a BIP68 violation (v2, lock 50, input 5 deep)", "bad-txns-nonfinal", [t_bad68], commit="good")
    ctl = build_block(template(A), [t_n], auxpow=True, commit="bad" if a.mutant == "corrupt-control" else "good")
    res = submit(A, ctl)
    c.ok(res is None and A.tip() == ctl.hash, "CONTROL: the same hand-built AuxPoW block with a correct commitment is accepted", res)
    if res is None:
        check_block(ctl.hash, "hand-built block")

    # ---- end state
    mm_mine(A, la, c)
    c.ok(same_tip(everyone, 60), "everyone on one tip", tips(everyone))
    n = A.height()
    c.ok(all(len({x.getblockhash(h) for x in everyone}) == 1 for h in range(n + 1)), "chains identical 0..%d on %d nodes" % (n, len(everyone)))
    c.ok(B.verifychain(4, 12) is True, "B verifychain level 4 over the last 12 blocks")
    mm = sum(1 for h in range(H, n + 1) if int(A.getblock(A.getblockhash(h))["versionHex"], 16) & VERSION_AUXPOW)
    c.ok(mm == n - H + 1, "every block from H on is a merge-mined (AuxPoW) block", "%d of %d" % (mm, n - H + 1))
    rc = c.finish("scenario 1 (%s%s)" % (a.mode, ", mutant " + a.mutant if a.mutant else ""))
finally:
    net.stop()
    if not a.keep and rc == 0:
        shutil.rmtree(a.work, ignore_errors=True)
sys.exit(rc)
