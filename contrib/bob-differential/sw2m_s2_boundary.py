#!/usr/bin/env python3
"""Scenario 2: the boundary (H-2, H-1, H, H+1) and a reorg across it, twice.

Two groups fork at H-3:
  X = EndTimes A + C : builds H-2..H+1 with the boundary probes. Block H-1 is the last
      pre-activation block: a block with witness data is refused there, while a
      BIP68-violating v2 spend and a negative CLTV are still valid (and get mined, to
      prove those rules are not early). At H both are refused and the witness spend is
      accepted. A witness-LESS spend of a P2WPKH output is refused at H-1 AND at H: on
      this code base that script rule is on for every block once a SegWit height is set.
  Y = EndTimes B (+ 0.14.1 in mode old) : builds H-2..H+2, legacy only, including a
      transaction that double-spends one of X's.
Then X meets Y (X side reorgs across H onto Y), then X is extended and overtakes (Y side,
0.14.1 included, reorgs across H onto X). After each: one tip everywhere, the mempool
holds exactly what is still valid, and a block mined from it is accepted by everyone.

--mutant segwit+1 : EndTimes nodes activate SegWit/CSV at H+1 -> must FAIL
--mutant segwit-1 : EndTimes nodes activate SegWit/CSV at H-1 -> must FAIL
"""
import sys
from sw2m import *  # noqa: F401,F403

ap = std_args(__doc__)
ap.add_argument("--mutant", choices=["segwit+1", "segwit-1"])
a = ap.parse_args()
H, c = a.height, Checks()
net = Net(a.work, a.newbin, a.ref, a.mode, H, a.port0)
off = {"segwit+1": 1, "segwit-1": -1}.get(a.mutant, 0)
hk = {"segwit": H + off, "csv": H + off}
rc = 1
try:
    A, C, B = net.new("A", **hk), net.new("C", **hk), net.new("B", **hk)
    old = net.old() if a.mode == "old" else None
    for n in (A, B):
        n.call("createwallet", "w"); n.wallet = "w"
    connect(A, C); connect(A, B)
    if old:
        connect(B, old); connect(A, old)
    everyone = [A, C, B] + ([old] if old else [])
    xs, ys = [A, C], [B] + ([old] if old else [])
    say("== scenario 2, mode %s: AuxPoW at %d, CLTV at %d, SegWit/CSV at %d%s" % (a.mode, net.auxpow, H, H + off, "  MUTANT " + a.mutant if a.mutant else ""))
    cid = lambda h: BOB_CHAIN_ID if h >= net.auxpow else 0

    la, lb = A.getnewaddress("", "legacy"), B.getnewaddress("", "legacy")
    A.generatetoaddress(H - 16, la)
    ka = A.getnewaddress("", "legacy")
    kpriv, kpub = A.dumpprivkey(ka), A.getaddressinfo(ka)["pubkey"]
    W, KL = p2wpkh_script(kpub), bytes.fromhex(A.getaddressinfo(ka)["scriptPubKey"])
    WS = p2sh_of(W)
    coins = Coins(A)
    coin = coins.take(100)
    v = sat(coin["amount"]) // 14
    outs = [(W, v)] * 6 + [(WS, v)] * 2 + [(KL, v)] * 4
    fund = sign_wallet(A, mktx([(coin["txid"], coin["vout"], 0xffffffff)],
                               outs + [(bytes.fromhex(coin["scriptPubKey"]), sat(coin["amount"]) - v * len(outs) - 100000)]))
    A.sendrawtransaction(fund.serialize().hex())
    A.lockunspent(False, [{"txid": fund.hash, "vout": i} for i in range(len(outs))])
    F = fund.hash
    for _ in range(3):
        A.sendtoaddress(lb, 10)
    A.generatetoaddress(13, la)
    c.ok(same_tip(everyone, 300) and A.height() == H - 3, "everyone at the fork point H-3", tips(everyone))
    forkpoint = A.tip()

    def wspend(i, seq=0xffffffff, version=1, wrapped=False):
        t = mktx([(F, i, seq)], [(KL, v - 50000)], version)
        return sign_key(A, t, [kpriv], [(F, i, WS if wrapped else W, v, W if wrapped else None)])

    def lspend(i, seq=0xffffffff, version=1, neg_cltv=False):
        t = sign_key(A, mktx([(F, i, seq)], [(KL, v - 50000)], version), [kpriv], [(F, i, KL, v, None)])
        if neg_cltv:
            t.vin[0].scriptSig = bytes(CScript([OP_1NEGATE, OP_CHECKLOCKTIMEVERIFY, OP_DROP])) + bytes(t.vin[0].scriptSig)
            t.rehash()
        return t

    # the double spend: U -> X's destination in branch X, U -> Y's destination in branch Y
    U = coins.take(100)
    uin = [(U["txid"], U["vout"], 0xffffffff)]
    t_ux = sign_wallet(A, mktx(uin, [(KL, sat(U["amount"]) - 100000)]))
    t_uy = sign_wallet(A, mktx(uin, [(bytes.fromhex(B.getaddressinfo(lb)["scriptPubKey"]), sat(U["amount"]) - 200000)]))

    say("== partition: X = A+C, Y = B%s" % ("+0.14.1" if old else ""))
    disconnect(A, B)
    if old:
        disconnect(A, old)

    # ---------------- branch X: the boundary ----------------
    def accept(b, label):
        res = submit(A, b)
        okk = c.ok(res is None and A.tip() == b.hash, "X block %d ACCEPTED: %s" % (A.height() if res is None else -1, label), res)
        c.ok(wait_until(lambda: C.tip() == A.tip(), 60), "   C takes it over P2P", tips(xs))
        return okk

    def reject(b, want, label):
        rejects(c, [A, C], b, want, "block %d: %s" % (A.height() + 1, label))

    say("== X, block H-2 (plain, a legacy wallet payment)")
    x_leg = A.sendtoaddress(A.getnewaddress("", "legacy"), 3)
    A.generatetoaddress(1, la)
    c.ok(A.height() == H - 2 and x_leg in block_txids(A, A.tip()), "X block H-2 mined with a legacy payment")

    say("== X, block H-1: the last block under the old rules")
    t = template(A)
    reject(build_block(t, [wspend(0)], chain_id=cid(H - 1), commit="good"), "unexpected-witness",
           "a witness spend (with commitment) one block early")
    reject(build_block(t, [wspend(0)], chain_id=cid(H - 1), commit="none"), "unexpected-witness",
           "a witness spend (no commitment) one block early")
    if a.mode == "all4":
        reject(build_block(t, [], chain_id=BOB_CHAIN_ID, commit="none", auxpow=True), "premature-auxpow", "a merge-mined block one block early")
        reject(build_block(t, [], chain_id=BOB_CHAIN_ID, commit="none"), "premature-chainid", "chain ID 0x00B0 one block early")
    hz_w = mktx([(F, 1, 0xffffffff)], [(KL, v - 50000)])            # P2WPKH output spent with NOTHING
    # (BOB) the witness script rule is height-gated like CSV and CLTV: before H a P2WPKH output is
    # anyone-can-spend for upgraded nodes exactly as it is for 0.14.1, so this goes INTO block H-1.
    hz_68 = lspend(8, seq=50, version=2)                            # BIP68 lock 50, input 14 deep
    hz_cltv = lspend(9, neg_cltv=True)                              # -1 CHECKLOCKTIMEVERIFY
    x_h1 = build_block(t, [hz_w, hz_68, hz_cltv], chain_id=cid(H - 1), commit="none")
    accept(x_h1, "a witness-LESS P2WPKH spend, a BIP68 violation and a negative CLTV are all still VALID at H-1 (all three rules are height-gated)")
    c.ok(A.height() == H - 1, "tip is H-1")

    say("== X, block H: the first block under all the new rules")
    t = template(A)
    reject(build_block(t, [mktx([(F, 2, 0xffffffff)], [(KL, v - 50000)])], commit="good"), "Witness program hash mismatch",
           "a witness-less P2WPKH spend")
    reject(build_block(t, [lspend(10, seq=50, version=2)], commit="good"), "bad-txns-nonfinal", "the BIP68 violation that was valid one block ago")
    reject(build_block(t, [lspend(11, neg_cltv=True)], commit="good"), "Negative locktime", "the negative CLTV that was valid one block ago")
    reject(build_block(t, [wspend(3)], commit="bad"), "bad-witness-merkle-match", "a witness spend under a wrong commitment")
    if a.mode == "all4":
        reject(build_block(t, [], chain_id=0, commit="good"), "bad-chainid", "a chain-ID-0 block at the AuxPoW height")
    t_w, t_wrap, t_68ok = wspend(3), wspend(6, wrapped=True), lspend(10, seq=5, version=2)
    x_h = build_block(t, [t_w, t_wrap, t_68ok, t_ux], commit="good", auxpow=(a.mode == "all4"))
    accept(x_h, "witness spend + P2SH-witness spend + satisfied BIP68 spend + legacy spend at H%s" % (" (merge-mined)" if a.mode == "all4" else ""))
    c.ok(A.height() == H, "tip is H")
    p, ok_, wit, _, _ = commitment_check(C, x_h.hash) if C.tip() == x_h.hash else (0, 0, 0, 0, 0)
    c.ok(p and ok_ and wit == 2, "   C's copy of block H has both witnesses and a correct commitment", "witness inputs=%s" % wit)

    say("== X, block H+1 (EndTimes miner, wallet bech32 payment and its witness spend)")
    x_bech = A.getnewaddress("", "bech32")
    x_pay = A.sendtoaddress(x_bech, 4)
    up = [u for u in A.listunspent(0) if u["txid"] == x_pay and u["address"] == x_bech][0]
    x_wsp = tx_from_hex(A.signrawtransactionwithwallet(legacy_hex(mktx([(x_pay, up["vout"], 0xffffffff)], [(KL, sat(4) - 50000)])))["hex"])
    A.sendrawtransaction(x_wsp.serialize().hex())
    A.generatetoaddress(1, la)
    x_tip = A.tip()
    c.ok(A.height() == H + 1 and {x_pay, x_wsp.hash} <= set(block_txids(A, x_tip)), "X block H+1 mined with both")
    p, ok_, wit, _, _ = commitment_check(A, x_tip)
    c.ok(p and ok_ and wit == 1, "   commitment correct, 1 witness input")
    c.ok(wait_until(lambda: C.tip() == x_tip, 60), "   C takes it over P2P")

    # ---------------- branch Y ----------------
    say("== Y: B mines H-2..H+2, legacy only, with the double spend")
    c.ok(B.tip() == forkpoint and (not old or old.tip() == forkpoint), "Y side still at the fork point")
    y_leg = [B.sendtoaddress(B.getnewaddress("", "legacy"), 2)]
    B.generatetoaddress(1, lb)
    y_first = B.tip()
    B.sendrawtransaction(t_uy.serialize().hex())
    B.generatetoaddress(1, lb)
    c.ok(t_uy.hash in block_txids(B, B.tip()), "Y block H-1 confirms the double spend of U")
    y_leg.append(B.sendtoaddress(B.getnewaddress("", "legacy"), 2))
    B.generatetoaddress(3, lb)
    y_tip = B.tip()
    c.ok(B.height() == H + 2, "Y tip is H+2")
    if old:
        old_follows(c, old, B, "branch Y through H+2")

    # ---------------- X meets Y ----------------
    say("== join: X side must reorg across H onto Y")
    connect(A, B)
    if old:
        connect(A, old)
    c.ok(wait_until(lambda: all(n.tip() == y_tip for n in everyone), 120), "everyone on Y's tip", tips(everyone))
    mp = set(A.getrawmempool())
    back = {"X legacy payment": x_leg, "witness spend": t_w.hash, "P2SH-witness spend": t_wrap.hash, "BIP68 spend": t_68ok.hash,
            "bech32 payment": x_pay, "its witness spend": x_wsp.hash}
    gone = {"double-spent tx": t_ux.hash, "BIP68 violation": hz_68.hash, "negative CLTV": hz_cltv.hash}
    c.ok(all(t in mp for t in back.values()), "A's mempool got back X's still-valid transactions",
         ", ".join(k for k, t in back.items() if t not in mp) or "all %d" % len(back))
    c.ok(not any(t in mp for t in gone.values()), "A's mempool did NOT take back what is now invalid or double-spent",
         ", ".join(k for k, t in gone.items() if t in mp) or "none of %d" % len(gone))
    c.ok(A.gettxout(F, 8) is not None and A.gettxout(F, 9) is not None, "the outputs spent by the H-1 rule-breakers on X are unspent again on Y")
    e = A.err("generatetoaddress", 1, la)
    c.ok(e == "", "A mines a block from that mempool", e)
    m_tip = A.tip()
    c.ok(wait_until(lambda: all(n.tip() == m_tip for n in everyone), 60), "everyone accepts it", tips(everyone))
    p, ok_, wit, _, _ = commitment_check(A, m_tip)
    c.ok(p and ok_ and wit >= 3 and A.getrawmempool() == [], "it carries the returned witness spends (%d witness inputs); A's mempool is empty" % wit)

    # ---------------- X overtakes ----------------
    say("== X overtakes: C goes back to X alone and extends it")
    disconnect(A, C)
    C.invalidateblock(y_first)
    c.ok(C.tip() == x_tip, "C is back on X's tip H+1", tips([C]))
    C.generatetoaddress(4, la)
    C.reconsiderblock(y_first)
    x_tip2 = C.tip()
    c.ok(C.height() == H + 5 and C.tip() == x_tip2, "X is now H+5, Y is H+3")
    connect(A, C)
    c.ok(wait_until(lambda: all(n.tip() == x_tip2 for n in everyone), 120), "everyone reorgs across H onto X%s" % (" (0.14.1 too)" if old else ""), tips(everyone))
    c.ok(all(n.getblockhash(H - 1) == x_h1.hash and n.getblockhash(H) == x_h.hash for n in everyone), "all have X's H-1 and H blocks in their chain")
    for n in (A, B, C):
        c.ok(t_uy.hash not in n.getrawmempool(), "%s's mempool does not hold Y's double spend" % n.name)
    e = A.err("generatetoaddress", 1, la)
    c.ok(e == "", "A mines a block from its mempool", e)
    f_tip = A.tip()
    c.ok(wait_until(lambda: all(n.tip() == f_tip for n in everyone), 60), "everyone accepts it", tips(everyone))
    for n in (A, B, C):
        c.ok(wait_until(lambda: n.getrawmempool() == [], 30), "%s mempool empty" % n.name, n.getrawmempool())
    if old:
        om = old.getrawmempool()
        c.ok(om == [], "0.14.1 mempool empty", om)
    n_ = A.height()
    c.ok(all(len({x.getblockhash(h) for x in everyone}) == 1 for h in range(n_ + 1)), "chains identical 0..%d on %d nodes" % (n_, len(everyone)))
    c.ok(C.verifychain(4, 12) is True and B.verifychain(4, 12) is True, "verifychain level 4 on B and C")
    rc = c.finish("scenario 2 (%s%s)" % (a.mode, ", mutant " + a.mutant if a.mutant else ""))
finally:
    net.stop()
    if not a.keep and rc == 0:
        shutil.rmtree(a.work, ignore_errors=True)
sys.exit(rc)
