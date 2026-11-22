#!/usr/bin/env python3
"""Scenario 4b: is a witness-less spend of a SegWit output NON-STANDARD for stock 0.14.1
under MAINNET policy? (sw2m_s4 part 2 cannot say: 0.14.1 switches standardness off on regtest.)

Two STOCK 0.14.1 daemons, M (miner) and R (a second node), are started with MAINNET
parameters on fresh datadirs. They are wired to each other only (-connect on loopback,
no DNS seeds, no discovery) so they never see the real network; they share a private
chain grown from the mainnet genesis block at mainnet minimum difficulty (about a million
scrypt hashes per block, mined by 0.14.1's own built-in miner; expect roughly an hour for
the 103 blocks needed to mature two coinbases). No EndTimes node is involved: the question
is only what an un-upgraded node and miner do by themselves.

Checked, each against M's sendrawtransaction, R's mempool (relay) and M's own next block:
  a. creating a P2SH-P2WPKH output (what a "3..."-style SegWit address is)
  b. spending it with NO witness, scriptSig = just the redeem script push   <- the theft
  c. creating a bare P2WPKH output (what a bech32 address is)
  d. spending a bare P2WPKH output with nothing (needs c on chain: put there in a
     hand-built block, ground in this script, because 0.14.1 will not relay c)

The brief's expectation is that b and d are refused by stock 0.14.1 ("the bad block
requires a modified miner"). The script exits non-zero if either is accepted.
"""
import argparse, hashlib, multiprocessing, os, shutil, struct, subprocess, sys, time
from sw2m import *  # noqa: F401,F403

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument("--ref", default=os.path.expanduser("~/bob-rebase/ref-0.14.1"))
ap.add_argument("--port0", type=int, default=28500)
ap.add_argument("--threads", type=int, default=3, help="0.14.1 miner threads / grinder processes")
ap.add_argument("--mine-timeout", type=int, default=6 * 3600)
ap.add_argument("--keep", action="store_true")
ap.add_argument("--resume", action="store_true", help="reuse the datadirs in the work dir (chain already mined)")
ap.add_argument("work")
a = ap.parse_args()
c = Checks()
rc = 1
B58 = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"


def p2pkh_of(addr):
    n = 0
    for ch in addr:
        n = n * 58 + B58.index(ch)
    raw = n.to_bytes(25, "big")
    assert hashlib.sha256(hashlib.sha256(raw[:21]).digest()).digest()[:4] == raw[21:], "bad base58 checksum"
    return bytes.fromhex("76a914") + raw[1:21] + bytes.fromhex("88ac")


def mainnet_node(name, p2p, rpc, peer_p2p):
    """A stock 0.14.1 daemon on MAINNET parameters that can only ever talk to 127.0.0.1:peer_p2p."""
    n = Node.__new__(Node)
    n.name, n.kind, n.p2p, n.rpc, n.wallet = name, "old", p2p, rpc, None
    n.auth = "ZDplZQ=="       # d:ee (on mainnet 0.14.1 refuses a password equal to the user name)
    n.dd = os.path.join(a.work, name)
    os.makedirs(n.dd, exist_ok=True)
    n.argv = [os.path.join(a.ref, "dobbscoind"), "-datadir=" + n.dd, "-rpcuser=d", "-rpcpassword=ee", "-rpcport=%d" % rpc,
              "-rpcbind=127.0.0.1", "-rpcallowip=127.0.0.1", "-port=%d" % p2p, "-bind=127.0.0.1", "-listen=1", "-server=1",
              "-printtoconsole=0", "-debug=net", "-dnsseed=0", "-dns=0", "-discover=0", "-upnp=0",
              "-connect=127.0.0.1:%d" % peer_p2p]
    n.env = dict(os.environ, LD_LIBRARY_PATH=os.path.join(a.ref, "lib"))
    n.start()
    return n


def _grind(args):
    head, target, start, step, stop = args
    n = start
    while n < 0xffffffff:
        if stop.is_set():
            return None
        for _ in range(2000):
            h = head + struct.pack("<I", n)
            if int.from_bytes(hashlib.scrypt(h, salt=h, n=1024, r=1, p=1, maxmem=2097152, dklen=32), "little") <= target:
                stop.set()
                return n
            n += step
    return None


def hand_block(node, txs):
    """A block on node's tip holding txs, proof of work ground here in parallel (mainnet difficulty)."""
    tip = node.getblock(node.tip())
    cb = create_coinbase(tip["height"] + 1)
    cb.vout[0].nValue = COIN
    cb.rehash()
    b = CBlock()
    b.nVersion, b.hashPrevBlock = tip["version"], int(tip["hash"], 16)
    b.nTime, b.nBits = max(int(time.time()), tip["time"] + 1), int(tip["bits"], 16)
    b.vtx = [cb] + list(txs)
    b.hashMerkleRoot = b.calc_merkle_root()
    b.nNonce = 0
    head = b.serialize()[:76]
    target = uint256_from_compact(b.nBits)
    t0 = time.time()
    with multiprocessing.Manager() as m:
        stop = m.Event()
        with multiprocessing.Pool(a.threads) as pool:
            found = [n for n in pool.map(_grind, [(head, target, i, a.threads, stop) for i in range(a.threads)]) if n is not None]
    b.nNonce = found[0]
    b.rehash()
    say("   (hand-built block %d ground in %.0fs, nBits %08x)" % (tip["height"] + 1, time.time() - t0, b.nBits))
    return b


def mine_to(M, height, label):
    """Let 0.14.1's own miner run until the chain is at least `height`."""
    t0, last = time.time(), M.height()
    if last >= height:
        return
    M.setgenerate(True, a.threads)
    try:
        while M.height() < height:
            if time.time() - t0 > a.mine_timeout:
                raise SystemExit("mining timed out at height %d" % M.height())
            time.sleep(5)
            h = M.height()
            if h != last and (h % 10 == 0 or height - h < 3):
                say("   ... %s: height %d after %.0fs" % (label, h, time.time() - t0))
            last = h
    finally:
        M.setgenerate(False)
    time.sleep(2)


def offer(M, R, tx, what):
    """sendrawtransaction to M; returns (accepted, reason, relayed_to_R)."""
    e = M.err("sendrawtransaction", legacy_hex(tx))
    inpool = tx.hash in M.getrawmempool()
    relayed = inpool and wait_until(lambda: tx.hash in R.getrawmempool(), 30)
    say("   0.14.1 (mainnet policy) sendrawtransaction %s -> %s; in M's mempool: %s; relayed to R: %s" %
        (what, e or "ACCEPTED", inpool, relayed))
    return inpool, e, relayed


try:
    if not a.resume and os.path.exists(a.work):
        shutil.rmtree(a.work)
    os.makedirs(a.work, exist_ok=True)
    net_nodes = []
    M = mainnet_node("M-0.14.1-mainnet", a.port0, a.port0 + 1, a.port0 + 2); net_nodes.append(M)
    R = mainnet_node("R-0.14.1-mainnet", a.port0 + 2, a.port0 + 3, a.port0); net_nodes.append(R)
    say("== scenario 4b: two stock 0.14.1 nodes, MAINNET parameters, private loopback-only chain")
    info = M.getinfo()
    c.ok(info.get("testnet") is False and M.getblockhash(0) == R.getblockhash(0) and not os.path.exists(os.path.join(M.dd, "regtest")),
         "both run MAINNET parameters (genesis %s, testnet=%s, no regtest/testnet subdir)" % (M.getblockhash(0)[:16], info.get("testnet")))
    c.ok(wait_until(lambda: len(M.getpeerinfo()) >= 1 and len(R.getpeerinfo()) >= 1, 120), "M and R are connected")
    peers = sorted({p["addr"].split(":")[0] for n in net_nodes for p in n.getpeerinfo()})
    c.ok(peers == ["127.0.0.1"], "their only peers are each other on loopback", peers)

    mine_to(M, 103, "maturing two coinbases")
    c.ok(wait_until(lambda: R.tip() == M.tip(), 120), "R follows M's chain", tips(net_nodes))
    say("   chain: height %d, difficulty %s, block version %s" % (M.height(), M.getdifficulty(), M.getblock(M.tip())["version"]))
    coins = sorted([u for u in M.listunspent(100)], key=lambda u: -u["confirmations"])
    c.ok(len(coins) >= 2, "M's wallet has two mature coinbases", len(coins))

    ka = M.getnewaddress()
    kpub = M.validateaddress(ka)["pubkey"]
    W = p2wpkh_script(kpub)
    WS = p2sh_of(W)
    KL = p2pkh_of(ka)
    thief = p2pkh_of(R.getnewaddress())

    def fund(coin, script, n):
        v = sat(coin["amount"]) // (n + 2)
        t = mktx([(coin["txid"], coin["vout"], 0xffffffff)], [(script, v)] * n + [(KL, sat(coin["amount"]) - v * n - 100000)])
        r = M.signrawtransaction(legacy_hex(t))
        assert r["complete"], r
        return tx_from_hex(r["hex"]), v

    # ---- a. create a P2SH-P2WPKH output
    say("== a. paying to a P2SH-P2WPKH output (a wrapped SegWit address)")
    fw, vw = fund(coins[0], WS, 3)
    ok_, e, rel = offer(M, R, fw, "tx paying 3 P2SH-P2WPKH outputs")
    c.ok(ok_ and rel, "(expected) 0.14.1 accepts and relays a payment TO a P2SH-P2WPKH output: to it, an ordinary P2SH payment", e)
    mine_to(M, M.height() + 1, "confirming it")
    c.ok(M.gettxout(fw.hash, 0, False) is not None, "it is confirmed in 0.14.1's own block %d" % M.height())

    # ---- b. the theft
    say("== b. THE THEFT: spending that P2SH-P2WPKH output with NO witness and NO signature")
    steal = mktx([(fw.hash, 0, 0xffffffff)], [(thief, vw - 100000)])
    steal.vin[0].scriptSig = bytes(CScript([bytes(W)]))
    steal.rehash()
    say("   the whole theft tx (%d bytes, scriptSig is only the 22-byte redeem script): %s" % (len(steal.serialize()), legacy_hex(steal)))
    ok_, e, rel = offer(M, R, steal, "witness-less spend of the P2SH-P2WPKH output")
    c.ok(not ok_, "stock 0.14.1 under MAINNET policy REFUSES the witness-less P2SH-P2WPKH spend (non-standard)",
         e or "it was ACCEPTED into the mempool as a standard transaction")
    c.ok(not rel, "stock 0.14.1 does not RELAY it to its peer", "R's mempool has it" if rel else "")
    if ok_:
        mine_to(M, M.height() + 1, "0.14.1 mining with the theft tx in its mempool")
        blk = M.getblock(M.tip())
        mined = steal.hash in blk["tx"]
        c.ok(not mined, "stock 0.14.1's own miner does NOT put it in a block",
             "block %d %s, mined by 0.14.1's built-in miner, contains it" % (blk["height"], blk["hash"][:16]) if mined else "")
        c.ok(wait_until(lambda: R.tip() == M.tip(), 60), "(recorded) the second 0.14.1 node accepts that block", tips(net_nodes))
        say("   R's wallet (the thief) now shows unconfirmed+confirmed balance: %s" % R.getbalance("*", 0))

    # ---- c. create a bare P2WPKH output
    say("== c. paying to a bare P2WPKH output (a bech32 address)")
    fb, vb = fund(coins[1], W, 2)
    ok_, e, rel = offer(M, R, fb, "tx paying 2 bare P2WPKH outputs")
    say("   NOTE stock 0.14.1 %s a payment TO a bare witness program%s" % ("ACCEPTS" if ok_ else "refuses", "" if ok_ else " (%s): only a miner can put one on chain" % e))
    if ok_:
        mine_to(M, M.height() + 1, "confirming it")
    else:
        hb = hand_block(M, [fb])
        res = submit(M, hb, witness=False)
        c.ok(res is None and M.tip() == hb.hash, "hand-built block with that payment accepted by 0.14.1 (it is consensus-valid)", res)
    c.ok(M.gettxout(fb.hash, 0, False) is not None, "a bare P2WPKH output is now on the chain")

    # ---- d. spend it with nothing
    say("== d. spending the bare P2WPKH output with an empty scriptSig")
    steal2 = mktx([(fb.hash, 0, 0xffffffff)], [(thief, vb - 100000)])
    ok_, e, rel = offer(M, R, steal2, "witness-less spend of the bare P2WPKH output")
    c.ok(not ok_, "stock 0.14.1 under MAINNET policy REFUSES the witness-less bare P2WPKH spend (non-standard)", e or "ACCEPTED")
    if ok_:
        mine_to(M, M.height() + 1, "0.14.1 mining with it")
        c.ok(steal2.hash not in M.getblock(M.tip())["tx"], "stock 0.14.1's own miner does NOT put it in a block")
    rc = c.finish("scenario 4b (0.14.1 mainnet policy)")
finally:
    for n in list(locals().get("net_nodes", [])):
        n.stop()
    if not a.keep and rc == 0:
        shutil.rmtree(a.work, ignore_errors=True)
sys.exit(rc)
