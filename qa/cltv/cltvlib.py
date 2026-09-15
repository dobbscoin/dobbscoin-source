"""Block- and transaction-building helpers for the (BOB) CLTV on-chain tests.

The unit tests in src/test/cltv_tests.cpp drive EvalScript directly. These build
real blocks on a regtest chain instead, which is the only way to test the wiring
between the interpreter and ConnectBlock -- in particular that the activation
height gate actually selects the flag.

The wallet is compiled out of the standard build, so everything here is hand
rolled: keys, coinbases, blocks, signatures.
"""
import base64
import hashlib
import json
import os
import re
import struct
import time
import urllib.error
import urllib.request

import ecdsa

CHAIN_ID = 0x00B0
VERSION_AUXPOW = 1 << 8

OP_CHECKSIG = 0xac
OP_CHECKLOCKTIMEVERIFY, OP_DROP = 0xb1, 0x75
SIGHASH_ALL = 1


# ---------------------------------------------------------------- plumbing

class Rpc(object):
    def __init__(self, conf):
        cfg = dict(re.findall(r"^(\w+)=(.*)$", open(conf).read(), re.M))
        self.url = "http://127.0.0.1:%s/" % cfg["rpcport"]
        self.auth = base64.b64encode(
            ("%s:%s" % (cfg["rpcuser"], cfg["rpcpassword"])).encode()).decode()

    def __call__(self, method, *params):
        req = urllib.request.Request(
            self.url,
            json.dumps({"id": 1, "method": method, "params": list(params)}).encode(),
            {"Content-Type": "application/json", "Authorization": "Basic " + self.auth})
        try:
            r = json.load(urllib.request.urlopen(req))
        except urllib.error.HTTPError as e:
            r = json.load(e)
        if r.get("error"):
            raise RuntimeError(r["error"].get("message", str(r["error"])))
        return r["result"]


def sha256d(b):
    return hashlib.sha256(hashlib.sha256(b).digest()).digest()


def scrypt_pow(h):
    return hashlib.scrypt(h, salt=h, n=1024, r=1, p=1, dklen=32)


def varint(n):
    if n < 0xfd:
        return bytes([n])
    if n <= 0xffff:
        return b"\xfd" + struct.pack("<H", n)
    return b"\xfe" + struct.pack("<I", n)


def vb(b):
    return varint(len(b)) + b


def push(data):
    """Minimal push of arbitrary data."""
    n = len(data)
    assert n < 0x4c, "only small pushes needed here"
    return bytes([n]) + data


def push_num(n):
    """CScriptNum-style minimal encoding, then a push."""
    if n == 0:
        return b"\x00"
    neg = n < 0
    n = abs(n)
    out = bytearray()
    while n:
        out.append(n & 0xff)
        n >>= 8
    if out[-1] & 0x80:
        out.append(0x80 if neg else 0x00)
    elif neg:
        out[-1] |= 0x80
    return push(bytes(out))


# ------------------------------------------------------------------- keys

class Key(object):
    def __init__(self, secret=None):
        self.sk = (ecdsa.SigningKey.from_string(secret, curve=ecdsa.SECP256k1)
                   if secret else ecdsa.SigningKey.generate(curve=ecdsa.SECP256k1))
        vk = self.sk.get_verifying_key()
        p = vk.pubkey.point
        # Compressed pubkey: the chain is fine with either, compressed is smaller.
        self.pubkey = bytes([2 + (p.y() & 1)]) + p.x().to_bytes(32, "big")

    def p2pk_script(self):
        """Pay-to-pubkey, not P2PKH: OpenSSL 3 dropped ripemd160 from its default
        provider, so hashlib cannot do hash160 here. P2PK needs no hash, and
        regtest does not require standard scripts."""
        return push(self.pubkey) + bytes([OP_CHECKSIG])

    def sign(self, sighash, high_s=False):
        """Strict-DER signature. BIP66 is enforced on this chain, so a sloppy
        encoding would be rejected for the wrong reason.

        high_s=True deliberately emits the HIGH-S form. Low-S is not a consensus
        rule here -- it lives in the standard flags only -- so a high-S signature
        is valid on this chain and MUST verify. libsecp256k1 accepts only low-S,
        so this is the case that catches a missing
        secp256k1_ecdsa_signature_normalize() in CPubKey::Verify, which would
        reject blocks an OpenSSL node accepts.
        """
        order = ecdsa.SECP256k1.order
        sig = self.sk.sign_digest(sighash, sigencode=ecdsa.util.sigencode_der)
        r, s = ecdsa.util.sigdecode_der(sig, order)
        if (s > order // 2) != high_s:
            s = order - s
        return ecdsa.util.sigencode_der(r, s, order)


def cltv_script(locktime, key):
    """<locktime> CHECKLOCKTIMEVERIFY DROP <pubkey> CHECKSIG"""
    return (push_num(locktime) + bytes([OP_CHECKLOCKTIMEVERIFY, OP_DROP])
            + push(key.pubkey) + bytes([OP_CHECKSIG]))


# ------------------------------------------------------------ transactions

def ser_tx(vin, vout, nlocktime=0):
    """vin: list of (txid_bytes_le, vout_index, scriptSig, nSequence)
       vout: list of (value_satoshi, scriptPubKey)"""
    out = struct.pack("<i", 1) + varint(len(vin))
    for txid, idx, script, seq in vin:
        out += txid + struct.pack("<I", idx) + vb(script) + struct.pack("<I", seq)
    out += varint(len(vout))
    for value, spk in vout:
        out += struct.pack("<q", value) + vb(spk)
    return out + struct.pack("<I", nlocktime)


def txid(raw):
    return sha256d(raw)


def signature_hash(vin, vout, nlocktime, in_index, script_code):
    """SIGHASH_ALL: every other input's scriptSig is blanked."""
    sign_vin = []
    for i, (t, idx, _script, seq) in enumerate(vin):
        sign_vin.append((t, idx, script_code if i == in_index else b"", seq))
    ser = ser_tx(sign_vin, vout, nlocktime) + struct.pack("<I", SIGHASH_ALL)
    return sha256d(ser)


def spend(prev_txid, prev_index, script_code, key, out_script, value,
          nlocktime=0, nsequence=0, high_s=False):
    """Build a 1-in 1-out transaction spending prev under script_code."""
    vin = [(prev_txid, prev_index, b"", nsequence)]
    vout = [(value, out_script)]
    sighash = signature_hash(vin, vout, nlocktime, 0, script_code)
    sig = key.sign(sighash, high_s=high_s) + bytes([SIGHASH_ALL])
    vin[0] = (prev_txid, prev_index, push(sig), nsequence)
    return ser_tx(vin, vout, nlocktime)


# ----------------------------------------------------------------- blocks

def coinbase(height, script_pubkey, value, extra=b""):
    """BIP34 requires the height as the first item of the coinbase scriptSig."""
    script_sig = push_num(height) + extra
    vin = [(b"\x00" * 32, 0xffffffff, script_sig, 0xffffffff)]
    return ser_tx(vin, [(value, script_pubkey)])


def merkle_root(txids):
    layer = list(txids)
    if len(layer) == 1:
        return layer[0]
    while len(layer) > 1:
        if len(layer) % 2:
            layer.append(layer[-1])
        layer = [sha256d(layer[i] + layer[i + 1]) for i in range(0, len(layer), 2)]
    return layer[0]


def mine_block(rpc, txs, key, value=None, version=None, ntime=None, nbits=None):
    """Assemble a block on the current tip containing txs, solve it, submit it.

    Returns the submitblock result: None on acceptance, else a reject reason.
    """
    tmpl = rpc("getblocktemplate")
    height = tmpl["height"]
    if nbits is None:
        nbits = int(tmpl["bits"], 16)
    target = int(tmpl["target"], 16) if "target" in tmpl else None
    if target is None:
        exp = nbits >> 24
        mant = nbits & 0xffffff
        target = mant * (1 << (8 * (exp - 3)))

    if value is None:
        value = tmpl["coinbasevalue"]
    cb = coinbase(height, key.p2pk_script(), value)
    all_tx = [cb] + list(txs)
    root = merkle_root([txid(t) for t in all_tx])

    # Past the AuxPoW fork every block must declare the chain ID.
    if version is None:
        version = 3 | (CHAIN_ID << 16) if height >= 10 else 3

    prev = bytes.fromhex(tmpl["previousblockhash"])[::-1]
    # Use the template's own curtime. The caller advances the node's clock with
    # setmocktime before each block, so curtime and bits are computed for the
    # same moment and agree with what the node will expect back.
    if ntime is None:
        ntime = int(tmpl["curtime"])
    for nonce in range(1 << 32):
        hdr = (struct.pack("<i", version) + prev + root
               + struct.pack("<I", ntime) + struct.pack("<I", nbits)
               + struct.pack("<I", nonce))
        if int.from_bytes(scrypt_pow(hdr), "little") <= target:
            break
    else:
        raise RuntimeError("unsolvable")

    blk = hdr + varint(len(all_tx)) + b"".join(all_tx)
    return rpc("submitblock", blk.hex()), txid(cb), height, value
