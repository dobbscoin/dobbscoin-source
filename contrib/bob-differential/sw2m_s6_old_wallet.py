#!/usr/bin/env python3
"""Scenario 6: the wallet and the node of someone who never upgraded, after activation.

Links: 0.14.1 <-> EndTimes A <-> EndTimes B. SegWit/CSV/CLTV at H = 150 (0.14.1's
compiled regtest CLTV height), AuxPoW at 10. The 0.14.1 wallet holds mature coins.

  wallet : asked to pay a bech32 address (regtest dcrt1..., and mainnet-style bob1...,
           P2WPKH and P2WSH, lower and upper case, plus 300 random ones) the 0.14.1 wallet
           must refuse with an error and create nothing: balance, transaction list and
           mempool unchanged. A P2SH-wrapped SegWit address is an ordinary P2SH address to
           0.14.1: paying it must work, and the EndTimes owner must be able to spend it.
  relay  : a legacy payment made by 0.14.1 reaches both EndTimes mempools and is mined by
           EndTimes; a legacy payment made on EndTimes B reaches 0.14.1's mempool.
  credit : a payment TO the 0.14.1 wallet whose inputs are witness spends is credited
           once confirmed.
  mining : 0.14.1 mines a block holding a legacy transaction. The block must be valid for
           EndTimes (shown by submitblock) AND must reach EndTimes over P2P by itself; the
           second half is the same question as scenario 3.

--mutant accept-bech32 : pretend the 0.14.1 wallet accepted the bech32 payment (flips the
                         expectation of the refusal checks) -> must FAIL
"""
import os, sys, time
from sw2m import *  # noqa: F401,F403
from test_framework.segwit_addr import encode_segwit_address  # noqa: E402

ap = std_args(__doc__)
ap.add_argument("--mutant", choices=["accept-bech32"])
ap.add_argument("--wait", type=int, default=60, help="seconds to wait for 0.14.1's block to arrive over P2P")
a = ap.parse_args()
if a.mode != "old":
    raise SystemExit("scenario 6 needs the 0.14.1 daemon: --mode old")
H, c = a.height, Checks()
net = Net(a.work, a.newbin, a.ref, a.mode, H, a.port0)
rc = 1


def old_mines(old, n=1):
    h0 = old.height()
    old.setgenerate(True, n)
    if not wait_until(lambda: old.height() >= h0 + n, 120):
        raise SystemExit("0.14.1 did not mine")
    return [old.getblockhash(h) for h in range(h0 + 1, h0 + n + 1)]


try:
    old = net.old()
    A, B = net.new("A"), net.new("B")
    for n in (A, B):
        n.call("createwallet", "w"); n.wallet = "w"
    connect(A, old); connect(B, A)
    trio = [A, B, old]
    say("== scenario 6: 0.14.1 <-> A <-> B, SegWit/CSV/CLTV at %d%s" % (H, "  MUTANT " + a.mutant if a.mutant else ""))
    old_mines(old, 4)                      # coins for the 0.14.1 wallet, mature long before H
    c.ok(wait_until(lambda: all(n.tip() == old.tip() for n in trio) and A.height() == 4, 180),
         "0.14.1 mined blocks 1-4 (its wallet's coins); everyone has them", tips(trio))
    la = A.getnewaddress("", "legacy")
    A.generatetoaddress(H + 2 - A.height(), la)
    c.ok(same_tip(trio, 300) and A.height() == H + 2, "all three at H+2 (past activation)", tips(trio))
    A.sendtoaddress(B.getnewaddress("", "legacy"), 50)
    A.generatetoaddress(1, la)
    same_tip(trio, 60)
    bal0 = old.getbalance()
    c.ok(bal0 > 0, "the 0.14.1 wallet has a spendable balance", bal0)

    # ---------------- wallet: bech32 must be refused ----------------
    say("== the 0.14.1 wallet is asked to pay bech32 addresses")
    bech = B.getnewaddress("", "bech32")
    c.ok(bech.startswith("dcrt1"), "EndTimes B hands out a bech32 address after activation", bech)
    prog20, prog32 = bytes.fromhex(B.getaddressinfo(bech)["witness_program"]), os.urandom(32)
    cases = [("regtest P2WPKH from B's wallet", bech),
             ("the same in upper case", bech.upper()),
             ("regtest P2WSH", encode_segwit_address("dcrt", 0, prog32)),
             ("mainnet-style bob1 P2WPKH", encode_segwit_address("bob", 0, prog20)),
             ("mainnet-style bob1 P2WSH", encode_segwit_address("bob", 0, prog32)),
             ("mainnet-style BOB1 upper case", encode_segwit_address("bob", 0, prog20).upper())]
    ntx0, mp0 = len(old.listtransactions("*", 1000)), old.getrawmempool()
    want_refusal = a.mutant != "accept-bech32"
    for label, addr in cases:
        valid = old.validateaddress(addr)["isvalid"]
        e_send = old.err("sendtoaddress", addr, 1)
        e_many = old.err("sendmany", "", {addr: 1})
        e_raw = old.err("createrawtransaction", [], {addr: 1})
        refused = (not valid) and e_send != "" and e_many != "" and e_raw != ""
        c.ok(refused == want_refusal, "0.14.1 refuses %s" % label,
             "%s: validateaddress.isvalid=%s; sendtoaddress: %s; sendmany: %s; createrawtransaction: %s" % (addr, valid, e_send or "SENT", e_many or "SENT", e_raw or "BUILT"))
    rnd = [encode_segwit_address("bob", 0, os.urandom(20)) for _ in range(200)] + [encode_segwit_address("bob", 0, os.urandom(32)) for _ in range(100)]
    took = [x for x in rnd if old.validateaddress(x)["isvalid"]]
    c.ok(not took, "0.14.1 calls all 300 random bob1 addresses invalid (none decodes as a base58 address by accident)", took[:3])
    c.ok(old.getbalance() == bal0 and len(old.listtransactions("*", 1000)) == ntx0 and old.getrawmempool() == mp0,
         "after all refusals: 0.14.1 balance, transaction list and mempool are unchanged", "balance %s" % old.getbalance())

    # ---------------- wallet: P2SH-wrapped SegWit address works ----------------
    say("== the 0.14.1 wallet pays a P2SH-wrapped SegWit address (an ordinary P2SH address to it)")
    wrapped = B.getnewaddress("", "p2sh-segwit")
    c.ok(old.validateaddress(wrapped)["isvalid"], "0.14.1 accepts the wrapped address as valid", wrapped)
    t_wrap = old.sendtoaddress(wrapped, 3)
    c.ok(wait_until(lambda: t_wrap in A.getrawmempool() and t_wrap in B.getrawmempool(), 60), "0.14.1's payment relays to both EndTimes mempools")

    # ---------------- relay both ways, EndTimes mines ----------------
    say("== ordinary legacy payments, both directions")
    t_o2a = old.sendtoaddress(A.getnewaddress("", "legacy"), 2)
    c.ok(wait_until(lambda: t_o2a in A.getrawmempool() and t_o2a in B.getrawmempool(), 60), "0.14.1 -> EndTimes: legacy payment reaches A's and B's mempool")
    oaddr = old.getnewaddress()
    t_b2o = B.sendtoaddress(oaddr, 4)
    c.ok(wait_until(lambda: t_b2o in old.getrawmempool(), 60), "EndTimes B -> 0.14.1: legacy payment reaches 0.14.1's mempool (through A)")
    A.generatetoaddress(1, la)
    blk = set(block_txids(A, A.tip()))
    c.ok({t_wrap, t_o2a, t_b2o} <= blk, "EndTimes mines all three in one block")
    c.ok(same_tip(trio, 60), "0.14.1 follows that block", tips(trio))
    c.ok(old.gettransaction(t_b2o)["confirmations"] == 1 and old.gettransaction(t_o2a)["confirmations"] == 1, "0.14.1's wallet sees its sent and received payments confirmed")
    c.ok(wait_until(lambda: B.getreceivedbyaddress(wrapped) == 3, 30), "B's wallet received the 3 (BOB) on its wrapped SegWit address")

    # ---------------- credit from witness spends ----------------
    say("== EndTimes pays the 0.14.1 wallet with witness inputs")
    wcoin = [u for u in B.listunspent(1) if u["address"] == wrapped][0]
    B.lockunspent(False, [{"txid": wcoin["txid"], "vout": wcoin["vout"]}])      # keep the wrapped coin for the payment below
    t_self = B.sendmany("", {B.getnewaddress("", "bech32"): 20, B.getnewaddress("", "bech32"): 5})
    wait_until(lambda: t_self in A.getrawmempool(), 60)
    A.generatetoaddress(1, la); same_tip(trio, 60)
    B.lockunspent(True)
    wit_coins = [u for u in B.listunspent(1) if u["address"] == wrapped or u.get("desc", "").startswith("wpkh(")]
    oaddr2 = old.getnewaddress()
    total = sum(u["amount"] for u in wit_coins)
    raw = B.createrawtransaction([{"txid": u["txid"], "vout": u["vout"]} for u in wit_coins], {oaddr2: float(total - Decimal("0.01"))})
    sp = tx_from_hex(B.signrawtransactionwithwallet(raw)["hex"])
    nwit = sum(1 for w in sp.wit.vtxinwit if w.scriptWitness.stack)
    c.ok(nwit == len(wit_coins) and nwit == 3, "B's payment to 0.14.1 spends %d witness inputs (2 bech32 coins and the wrapped coin 0.14.1 paid)" % nwit)
    B.sendrawtransaction(sp.serialize().hex())
    seen = wait_until(lambda: sp.hash in old.getrawmempool(), 15)
    say("   NOTE regtest 0.14.1 has the (witness-stripped) payment in its mempool before it confirms: %s" % seen)
    c.ok(wait_until(lambda: sp.hash in A.getrawmempool(), 30), "it reaches A's mempool")
    balb = old.getbalance()
    A.generatetoaddress(1, la)
    c.ok(sp.hash in block_txids(A, A.tip()) and same_tip(trio, 60), "confirmed by EndTimes; 0.14.1 follows the block", tips(trio))
    c.ok(wait_until(lambda: old.getreceivedbyaddress(oaddr2, 1) == total - Decimal("0.01"), 30), "the 0.14.1 wallet is credited with it",
         "received %s" % old.getreceivedbyaddress(oaddr2, 1))
    t_respend = old.sendtoaddress(A.getnewaddress("", "legacy"), float(old.getbalance() - 1))   # forces it to use that coin
    c.ok(wait_until(lambda: t_respend in A.getrawmempool(), 60), "0.14.1 can spend what it received from witness inputs; EndTimes takes the spend")
    A.generatetoaddress(1, la)
    c.ok(t_respend in block_txids(A, A.tip()) and same_tip(trio, 60), "and it confirms", tips(trio))

    # ---------------- 0.14.1 mines ----------------
    say("== 0.14.1 mines a block with a legacy transaction in it")
    t_m = A.sendtoaddress(old.getnewaddress(), 1)
    c.ok(wait_until(lambda: t_m in old.getrawmempool(), 60), "a legacy payment from EndTimes is in 0.14.1's mempool")
    tip0 = A.tip()
    (ob,) = old_mines(old)
    c.ok(t_m in old.getblock(ob)["tx"], "0.14.1 mined it into its own block %d" % old.height())
    got = wait_until(lambda: A.tip() == ob, a.wait)
    c.ok(got, "that 0.14.1 block reaches EndTimes A over P2P by itself (waited %ds)" % a.wait,
         "" if got else "NEVER: A still at %d/%s, 0.14.1 at %d/%s; A's view of the block: %s" %
         (A.height(), A.tip()[:12], old.height(), ob[:12],
          next((t["status"] for t in A.getchaintips() if t["hash"] == ob), "unknown")))
    if not got:
        res = A.submitblock(old.getblock(ob, False))
        c.ok(A.tip() == ob and wait_until(lambda: B.tip() == ob, 30), "handed over with submitblock, EndTimes accepts 0.14.1's block (it is valid) and relays it to B", res)
    c.ok(A.gettransaction(t_m)["confirmations"] == 1, "the payment is confirmed on EndTimes in 0.14.1's block")
    A.generatetoaddress(1, la)
    n_ = A.height()
    c.ok(same_tip(trio, 60) and all(len({x.getblockhash(h) for x in trio}) == 1 for h in range(n_ + 1)), "end state: chains identical 0..%d" % n_, tips(trio))
    rc = c.finish("scenario 6" + (" (mutant %s)" % a.mutant if a.mutant else ""))
finally:
    net.stop()
    if not a.keep and rc == 0:
        shutil.rmtree(a.work, ignore_errors=True)
sys.exit(rc)
