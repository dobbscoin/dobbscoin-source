#!/usr/bin/env python3
"""Shared plumbing for the "SegWit+CSV at 2,000,000" try-to-break rehearsal (sw2m_s*.py).

Mainnet plan: AuxPoW, CLTV, SegWit and CSV all activate at ONE height (2,000,000).
On regtest the EndTimes node takes -auxpowheight/-bip65height/-segwitheight/-csvheight,
so all four can sit at one height H. The real 0.14.1 reference daemon cannot: its regtest
heights are compiled in (AuxPoW 10, CLTV 150). So every scenario has two modes:

  --mode old   0.14.1 takes part. H = 150 for CLTV+SegWit+CSV, AuxPoW at 10 (not aligned).
  --mode all4  EndTimes nodes only. AuxPoW = CLTV = SegWit = CSV = H.

Nothing here touches node code: nodes are driven over RPC, blocks and transactions are
built with test/functional/test_framework and handed over with submitblock.
"""
import json, os, shutil, struct, subprocess, sys, time, urllib.request, urllib.error
from decimal import Decimal
from io import BytesIO

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(SRC, "test", "functional"))
from test_framework.blocktools import create_coinbase, add_witness_commitment  # noqa: E402
from test_framework.messages import (CAuxPow, CBlock, CBlockHeader, COutPoint, CTransaction, CTxIn, CTxOut,  # noqa: E402
                                     COIN, VERSION_AUXPOW, hash256, ser_uint256, uint256_from_compact)
from test_framework.script import (CScript, OP_TRUE, OP_0, OP_HASH160, OP_EQUAL, OP_CHECKLOCKTIMEVERIFY, OP_DROP,  # noqa: E402
                                   OP_CHECKSIG, OP_1NEGATE, LegacySignatureHash, SIGHASH_ALL, hash160)
from test_framework.key import ECKey  # noqa: E402

BOB_CHAIN_ID = 0x00B0
OLD_AUXPOW, OLD_CLTV = 10, 150       # compiled into 0.14.1 for regtest
H_DEFAULT = 150
MM_MAGIC = b'\xfa\xbe' + b'mm'
COMMIT_HDR = "6a24aa21a9ed"


def say(*a):
    print(*a, flush=True)


class Checks:
    def __init__(self):
        self.n, self.problems = 0, []

    def ok(self, cond, label, detail=""):
        self.n += 1
        if cond:
            say("   ok  %s%s" % (label, ("  [%s]" % detail) if detail else ""))
        else:
            self.problems.append(label + (": " + str(detail) if detail else ""))
            say("   !!  FAILED: %s%s" % (label, ("  [%s]" % detail) if detail else ""))
        return bool(cond)

    def finish(self, title):
        say("\n%s: %d checks, %d problem(s)" % (title, self.n, len(self.problems)))
        for p in self.problems:
            say("  PROBLEM " + p)
        say("RESULT: " + ("PASS" if not self.problems else "FAIL"))
        return 0 if not self.problems else 1


class RpcError(RuntimeError):
    def __init__(self, method, err):
        super().__init__("%s: %s" % (method, err))
        self.code, self.message = err.get("code"), err.get("message", "")


class Node:
    """One daemon. kind 'new' = EndTimes 0.21.2, 'old' = the 0.14.1 reference."""
    auth = "ZDpk"     # base64 of the RPC user:password, d:d

    def __init__(self, work, name, kind, binary, p2p, rpc, extra=(), env=None):
        self.name, self.kind, self.p2p, self.rpc = name, kind, p2p, rpc
        self.dd = os.path.join(work, name)
        os.makedirs(self.dd, exist_ok=True)
        self.argv = [binary, "-regtest", "-datadir=" + self.dd, "-rpcuser=d", "-rpcpassword=d",
                     "-rpcport=%d" % rpc, "-port=%d" % p2p, "-bind=127.0.0.1", "-listen=1", "-server=1",
                     "-printtoconsole=0", "-debug=net", "-dnsseed=0", "-discover=0"] + list(extra)
        self.env = env
        self.wallet = None
        self.start()

    def start(self):
        self.proc = subprocess.Popen(self.argv, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                     stderr=subprocess.DEVNULL, env=self.env)
        for _ in range(240):
            try:
                self.call("getblockcount"); return
            except Exception:
                if self.proc.poll() is not None:
                    raise SystemExit("%s exited at start (rc %s); see %s" % (self.name, self.proc.returncode, self.log))
                time.sleep(0.25)
        raise SystemExit("%s did not start" % self.name)

    @property
    def log(self):
        return os.path.join(self.dd, "regtest", "debug.log")

    @property
    def addr(self):
        return "127.0.0.1:%d" % self.p2p

    def call(self, method, *params):
        url = "http://127.0.0.1:%d/%s" % (self.rpc, "wallet/" + self.wallet if self.wallet else "")
        req = urllib.request.Request(url, data=json.dumps({"id": 1, "method": method, "params": list(params)}).encode(),
                                     headers={"Authorization": "Basic " + self.auth, "Content-Type": "application/json"})
        try:
            r = json.loads(urllib.request.urlopen(req, timeout=300).read(), parse_float=Decimal)
        except urllib.error.HTTPError as e:
            r = json.loads(e.read(), parse_float=Decimal)
        if r.get("error"):
            raise RpcError(method, r["error"])
        return r["result"]

    def __getattr__(self, method):
        if method.startswith("_"):
            raise AttributeError(method)
        return lambda *p: self.call(method, *p)

    def err(self, method, *params):
        """Call expecting an RPC error; return its message ('' if the call succeeded)."""
        try:
            self.call(method, *params)
        except RpcError as e:
            return e.message or "error"
        return ""

    def tip(self):
        return self.call("getbestblockhash")

    def height(self):
        return self.call("getblockcount")

    def grep(self, needle, since=0):
        """debug.log lines containing needle, from byte offset since."""
        with open(self.log, "rb") as f:
            f.seek(since)
            return [l.decode("utf8", "replace").rstrip() for l in f if needle.encode() in l]

    def logpos(self):
        return os.path.getsize(self.log)

    def stop(self):
        try:
            self.call("stop")
        except Exception:
            pass
        try:
            self.proc.wait(60)
        except Exception:
            self.proc.kill()


class Net:
    """Start nodes for a scenario. base = <~/bob-rebase>, holding ref-0.14.1/; newbin = EndTimes dobbscoind."""

    def __init__(self, work, newbin, refdir, mode, H=H_DEFAULT, port0=28100, fresh=True):
        self.work, self.newbin, self.refdir, self.mode, self.H = work, newbin, refdir, mode, H
        self.port0, self.nodes = port0, []
        if fresh and os.path.exists(work):
            shutil.rmtree(work)
        os.makedirs(work, exist_ok=True)
        if mode == "old" and H != OLD_CLTV:
            raise SystemExit("--mode old needs H=%d: 0.14.1's regtest CLTV height is compiled in" % OLD_CLTV)
        self.auxpow = OLD_AUXPOW if mode == "old" else H

    def heights(self, segwit=None, csv=None, cltv=None, auxpow=None):
        H = self.H
        return ["-auxpowheight=%d" % (self.auxpow if auxpow is None else auxpow),
                "-bip65height=%d" % (H if cltv is None else cltv),
                "-segwitheight=%d" % (H if segwit is None else segwit), "-allowwrappedsegwit=1",
                "-csvheight=%d" % (H if csv is None else csv)]

    def new(self, name, extra=(), **hk):
        i = len(self.nodes)
        n = Node(self.work, name, "new", self.newbin, self.port0 + 2 * i, self.port0 + 2 * i + 1,
                 self.heights(**hk) + ["-listenonion=0", "-natpmp=0", "-acceptnonstdtxn=0", "-par=1"] + list(extra))
        self.nodes.append(n)
        return n

    def old(self, name="old-0.14.1", extra=()):
        i = len(self.nodes)
        env = dict(os.environ, LD_LIBRARY_PATH=os.path.join(self.refdir, "lib"))
        n = Node(self.work, name, "old", os.path.join(self.refdir, "dobbscoind"), self.port0 + 2 * i,
                 self.port0 + 2 * i + 1, ["-upnp=0"] + list(extra), env)
        self.nodes.append(n)
        return n

    def stop(self):
        for n in self.nodes:
            n.stop()


def wait_until(fn, timeout=60, step=0.25):
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            if fn():
                return True
        except Exception:
            pass
        time.sleep(step)
    return False


def connect(a, b, timeout=30):
    """a (EndTimes) opens a manual connection to b."""
    a.call("addnode", b.addr, "onetry")
    if not wait_until(lambda: any(p["addr"] == b.addr for p in a.call("getpeerinfo")), timeout):
        raise SystemExit("%s could not connect to %s" % (a.name, b.name))
    # version handshake done on both sides
    wait_until(lambda: all(p.get("version", 0) for p in a.call("getpeerinfo")), 10)


def disconnect(a, b):
    """Drop every link between a and b. Links are always opened by an EndTimes node (0.14.1 has no disconnectnode)."""
    for x, y in ((a, b), (b, a)):
        if x.kind != "new":
            continue
        for p in x.call("getpeerinfo"):
            if p["addr"] == y.addr:
                try:
                    x.call("disconnectnode", "", p["id"])
                except RpcError:
                    pass
    if not wait_until(lambda: not linked(a, b), 20):
        raise SystemExit("could not disconnect %s from %s" % (a.name, b.name))


def linked(a, b):
    try:
        if any(p["addr"] == b.addr for p in a.call("getpeerinfo")):
            return True
        return any(p["addr"] == a.addr for p in b.call("getpeerinfo"))
    except Exception:
        return False


def same_tip(nodes, timeout=60):
    want = nodes[0].tip()
    return wait_until(lambda: all(n.tip() == want for n in nodes), timeout)


def tips(nodes):
    return ", ".join("%s=%d/%s" % (n.name, n.height(), n.tip()[:12]) for n in nodes)


# ---- scripts, transactions --------------------------------------------------------------

def p2wpkh_script(pubkey_hex):
    return CScript([OP_0, hash160(bytes.fromhex(pubkey_hex))])


def p2sh_of(script):
    return CScript([OP_HASH160, hash160(bytes(script)), OP_EQUAL])


def tx_from_hex(h):
    tx = CTransaction()
    tx.deserialize(BytesIO(bytes.fromhex(h)))
    tx.rehash()
    return tx


def mktx(ins, outs, version=1, locktime=0):
    """ins: [(txid_hex, vout, sequence)], outs: [(scriptPubKey bytes, satoshis)]."""
    tx = CTransaction()
    tx.nVersion, tx.nLockTime = version, locktime
    tx.vin = [CTxIn(COutPoint(int(t, 16), v), b"", s) for t, v, s in ins]
    tx.vout = [CTxOut(int(a), bytes(s)) for s, a in outs]
    tx.rehash()
    return tx


def sat(amount):
    return int(Decimal(str(amount)) * COIN)


def legacy_hex(tx):
    return tx.serialize_without_witness().hex()


def sign_wallet(node, tx):
    r = node.call("signrawtransactionwithwallet", legacy_hex(tx))
    if not r["complete"]:
        raise RuntimeError("wallet could not sign: %s" % r.get("errors"))
    return tx_from_hex(r["hex"])


def sign_key(node, tx, privkeys, prevs):
    """prevs: [(txid, vout, scriptPubKey bytes, satoshis, redeemScript bytes or None)]."""
    pv = []
    for t, v, spk, amt, rs in prevs:
        d = {"txid": t, "vout": v, "scriptPubKey": bytes(spk).hex(), "amount": float(Decimal(amt) / COIN)}
        if rs is not None:
            d["redeemScript"] = bytes(rs).hex()
        pv.append(d)
    r = node.call("signrawtransactionwithkey", legacy_hex(tx), privkeys, pv)
    if not r["complete"]:
        raise RuntimeError("could not sign: %s" % r.get("errors"))
    return tx_from_hex(r["hex"])


def strip_witness(tx):
    return tx_from_hex(legacy_hex(tx))


def has_witness(tx):
    return not tx.wit.is_null()


def utxos(node, minconf=1, min_amount=1, legacy_only=True):
    us = [u for u in node.call("listunspent", minconf) if u["amount"] >= min_amount and u["spendable"]]
    if legacy_only:
        us = [u for u in us if u["scriptPubKey"].startswith("76a914")]
    return sorted(us, key=lambda u: -u["confirmations"])


def cltv_script(lock, key):
    """<lock> CLTV DROP <pubkey> CHECKSIG, and its P2SH scriptPubKey."""
    redeem = CScript([lock, OP_CHECKLOCKTIMEVERIFY, OP_DROP, key.get_pubkey().get_bytes(), OP_CHECKSIG])
    return redeem, p2sh_of(redeem)


def sign_cltv(tx, i, redeem, key):
    h, err = LegacySignatureHash(redeem, tx, i, SIGHASH_ALL)
    assert err is None
    tx.vin[i].scriptSig = CScript([key.sign_ecdsa(h) + bytes([SIGHASH_ALL]), bytes(redeem)])
    tx.rehash()
    return tx


def new_key():
    k = ECKey()
    k.generate()
    return k


class Coins:
    """Hands out distinct mature wallet UTXOs of an EndTimes wallet (never the same one twice)."""

    def __init__(self, node):
        self.node, self.used = node, set()

    def take(self, minconf=1, min_amount=1):
        for u in utxos(self.node, minconf, min_amount):
            k = (u["txid"], u["vout"])
            if k not in self.used:
                self.used.add(k)
                self.node.call("lockunspent", False, [{"txid": u["txid"], "vout": u["vout"]}])
                return u
        raise RuntimeError("no spendable coin with %d confirmations on %s" % (minconf, self.node.name))


# ---- blocks -----------------------------------------------------------------------------

def make_auxpow(block_hash_hex, nbits, parent_valid=True):
    """A parent coinbase committing to the aux block hash, parent header ground to (or past) nbits."""
    payload = MM_MAGIC + bytes.fromhex(block_hash_hex) + struct.pack("<II", 1, 0)
    cb = CTransaction()
    cb.vin = [CTxIn(COutPoint(0, 0xffffffff), CScript([payload]), 0xffffffff)]
    cb.vout = [CTxOut(0, CScript([OP_TRUE]))]
    cb.rehash()
    ap = CAuxPow()
    ap.nVersion, ap.vin, ap.vout, ap.nLockTime = cb.nVersion, cb.vin, cb.vout, cb.nLockTime
    ap.hashBlock, ap.vMerkleBranch, ap.nIndex = 0, [], 0
    ap.vChainMerkleBranch, ap.nChainIndex = [], 0
    p = CBlockHeader()
    p.nVersion = 2
    p.hashPrevBlock, p.hashMerkleRoot, p.nTime, p.nBits, p.nNonce = 0, cb.sha256, 1, nbits, 0
    target = uint256_from_compact(nbits)
    p.rehash()
    while (p.scrypt256 <= target) != parent_valid:
        p.nNonce += 1
        p.rehash()
    ap.parentBlock = p
    return ap


def mm_mine(node, payout, c=None, label=None):
    """One block through the merge-mining RPCs. Returns the block hash."""
    t = node.call("createauxblock", payout)
    ap = make_auxpow(t["hash"], int(t["bits"], 16))
    res = node.call("submitauxblock", t["hash"], ap.serialize().hex())
    if c is not None:
        c.ok(res is True and node.tip() == t["hash"], label or "merge-mined block %d accepted" % t["height"], "submitauxblock=%s" % res)
    return t["hash"]


def template(node):
    """Just prev/height/bits/time of the next block, from the node itself."""
    try:
        g = node.call("getblocktemplate", {"rules": ["segwit"]}) if node.kind == "new" else node.call("getblocktemplate")
        return {"prev": g["previousblockhash"], "height": g["height"], "bits": int(g["bits"], 16),
                "time": max(g["curtime"], g.get("mintime", 0))}
    except RpcError:
        tip = node.call("getblock", node.tip())
        return {"prev": tip["hash"], "height": tip["height"] + 1, "bits": int(tip["bits"], 16),
                "time": max(int(time.time()), tip["time"] + 1)}


def build_block(tpl, txs=(), *, chain_id=BOB_CHAIN_ID, base=3, auxpow=False, commit="good", cb_script=None, cb_sats=COIN,
                time_bump=0):
    """A block on tpl. commit: 'good' (BIP141 commitment), 'none', 'bad' (one bit flipped),
    'stale' (commitment computed, then the witness of the last tx altered)."""
    cb = create_coinbase(tpl["height"])
    cb.vout[0].nValue = cb_sats
    cb.vout[0].scriptPubKey = bytes(cb_script) if cb_script is not None else CScript([OP_TRUE])
    cb.rehash()
    b = CBlock()
    b.nVersion = base + (chain_id << 16)
    b.hashPrevBlock = int(tpl["prev"], 16)
    b.nTime = tpl["time"] + time_bump
    b.nBits = tpl["bits"]
    b.vtx = [cb] + list(txs)
    b.hashMerkleRoot = b.calc_merkle_root()
    if commit != "none":
        add_witness_commitment(b)
        if commit == "bad":
            spk = bytearray(b.vtx[0].vout[-1].scriptPubKey)
            spk[-1] ^= 1
            b.vtx[0].vout[-1].scriptPubKey = bytes(spk)
            b.vtx[0].rehash()
            b.hashMerkleRoot = b.calc_merkle_root()
    if auxpow:
        b.nVersion |= VERSION_AUXPOW
        b.rehash()
        target = uint256_from_compact(b.nBits)
        while b.scrypt256 <= target:      # own hash MISSES: only the parent's work counts
            b.nNonce += 1
            b.rehash()
        b.auxpow = make_auxpow(b.hash, b.nBits)
    else:
        b.rehash()
        target = uint256_from_compact(b.nBits)
        while b.scrypt256 > target:
            b.nNonce += 1
            b.rehash()
    return b


def block_hex(b, witness=True):
    return b.serialize(with_witness=witness).hex()


def submit(node, b, witness=None):
    """submitblock; returns None when accepted, else the reject string."""
    if witness is None:
        witness = node.kind == "new"
    try:
        return node.call("submitblock", block_hex(b, witness))
    except RpcError as e:
        return "rpc-error: " + e.message


def is_invalid_on(node, blockhash, timeout=20):
    """The node knows the block and has marked it invalid (getchaintips)."""
    return wait_until(lambda: any(t["hash"] == blockhash and t["status"] == "invalid" for t in node.call("getchaintips")), timeout)


def rejects(c, nodes, b, want, label, witness=None):
    """Every EndTimes node in nodes must refuse block b. The first one's submitblock answer must name the
    reason; the others may answer 'duplicate' (the first node announces a compact block before it has
    connected it), so for all of them the proof is: tip unchanged and the block marked invalid."""
    for i, n in enumerate(nodes):
        tip0 = n.tip()
        res = submit(n, b, witness)
        named = res is not None and want in str(res)
        how = "reason named"
        # an EndTimes node may already have fetched the block over P2P from a 0.14.1 peer and refused it
        if not named and res is not None and (str(res).startswith("duplicate-invalid") or
                                              (i > 0 and str(res).startswith(("duplicate", "inconclusive")))):
            # "duplicate-invalid" is itself the node saying it holds the block marked invalid
            named = str(res).startswith("duplicate-invalid") or is_invalid_on(n, b.hash)
            how = "already had it; marked invalid=%s" % named
        c.ok(named and n.tip() == tip0, "%s rejects %s" % (n.name, label), "submitblock=%s, %s" % (res, how))


def commitment_check(node, blockhash):
    """Recompute the BIP141 witness commitment of a block from its raw bytes.
    Returns (present, correct, witness_inputs, is_auxpow, chain_id)."""
    raw = node.call("getblock", blockhash, 0)
    b = CBlock()
    b.deserialize(BytesIO(bytes.fromhex(raw)))
    cb = b.vtx[0]
    found = [bytes(o.scriptPubKey) for o in cb.vout if bytes(o.scriptPubKey).hex().startswith(COMMIT_HDR)]
    wit_inputs = sum(1 for tx in b.vtx[1:] for w in tx.wit.vtxinwit if w.scriptWitness.stack)
    aux, chain = bool(b.nVersion & VERSION_AUXPOW), (b.nVersion >> 16)
    if not found:
        return False, False, wit_inputs, aux, chain
    try:
        nonce = cb.wit.vtxinwit[0].scriptWitness.stack[0]
    except IndexError:
        return True, False, wit_inputs, aux, chain
    want = hash256(ser_uint256(b.calc_witness_merkle_root()) + nonce)
    return True, found[-1][6:38] == want, wit_inputs, aux, chain


def block_txids(node, blockhash):
    return node.call("getblock", blockhash)["tx"]


def old_follows(c, old, ref, label, timeout=60):
    want, h = ref.tip(), ref.height()
    got = wait_until(lambda: old.height() >= h and old.call("getblockhash", h) == want, timeout)
    return c.ok(got, "0.14.1 follows: " + label, "h=%d %s" % (h, want[:12]) if got else "0.14.1 at %d/%s, want %d/%s" % (old.height(), old.tip()[:12], h, want[:12]))


def std_args(desc):
    import argparse
    ap = argparse.ArgumentParser(description=desc)
    ap.add_argument("--mode", choices=["old", "all4"], default="old")
    ap.add_argument("--height", type=int, default=H_DEFAULT, help="activation height H (must be 150 with --mode old)")
    ap.add_argument("--newbin", default=os.path.join(SRC, "src", "dobbscoind"))
    ap.add_argument("--ref", default=os.path.expanduser("~/bob-rebase/ref-0.14.1"))
    ap.add_argument("--port0", type=int, default=28100)
    ap.add_argument("--keep", action="store_true", help="keep the work dir (datadirs + debug.log) afterwards")
    ap.add_argument("work")
    return ap
