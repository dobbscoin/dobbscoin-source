"""Shared helpers for the (BOB) AuxPoW parent-PoW tests.

Builds a synthetic "parent chain" block carrying a real scrypt proof of work
that commits to a (BOB) aux block -- the piece regtest-smoke.sh deliberately
leaves out. On regtest the target is easy enough that a parent solves in a
handful of scrypt attempts in pure Python, so no external miner is needed.
"""
import base64, hashlib, json, os, re, struct, time, urllib.request, urllib.error

CHAIN_ID = 0x00B0                       # AUXPOW_CHAIN_ID
MAGIC    = bytes([0xfa, 0xbe]) + b"mm"  # pchMergedMiningHeader


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


def scrypt_pow(header80):
    """scrypt_1024_1_1_256 -- password and salt are both the 80-byte header."""
    return hashlib.scrypt(header80, salt=header80, n=1024, r=1, p=1, dklen=32)


def varint(n):
    if n < 0xfd:
        return bytes([n])
    if n <= 0xffff:
        return b"\xfd" + struct.pack("<H", n)
    return b"\xfe" + struct.pack("<I", n)


def varbytes(b):
    return varint(len(b)) + b


def commitment(aux_hash_hex, tree_size=1, nonce=0, magic=True, extra_magic=False,
               root_override=None, truncate=False):
    """The fabe6d6d commitment as it appears in the parent coinbase scriptSig.

    CAuxPow::check() byte-reverses the computed root before searching for it,
    and the RPC hash hex is already that reversed (display) order -- so the
    bytes to embed are simply bytes.fromhex(hash_hex).
    """
    root = root_override if root_override is not None else bytes.fromhex(aux_hash_hex)
    s = b""
    if magic:
        s += MAGIC
    if extra_magic:
        s = MAGIC + s
    s += root + struct.pack("<I", tree_size)
    if not truncate:
        s += struct.pack("<I", nonce)
    return s


def coinbase(script):
    """A minimal single-input, single-output parent-chain coinbase."""
    tx = struct.pack("<i", 1)
    tx += varint(1) + b"\x00" * 32 + struct.pack("<I", 0xffffffff) \
        + varbytes(script) + struct.pack("<I", 0xffffffff)
    tx += varint(1) + struct.pack("<q", 0) + varbytes(b"\x51")
    return tx + struct.pack("<I", 0)


def solve_parent(prev_hex, merkle, nbits, target, version=1, solve=True, limit=1 << 24):
    """Grind a parent header until its scrypt hash meets the AUX block target.

    version defaults to 1 so the chain ID of the parent is 0 -- CAuxPow::check
    rejects a parent claiming our own chain ID.
    """
    ntime = int(time.time())
    for nonce in range(limit):
        hdr = (struct.pack("<i", version) + bytes.fromhex(prev_hex)[::-1] + merkle
               + struct.pack("<I", ntime) + struct.pack("<I", nbits)
               + struct.pack("<I", nonce))
        meets = int.from_bytes(scrypt_pow(hdr), "little") <= target
        # solve=False must return a header that provably FAILS the check. Do
        # not just hand back nonce 0 and assume it is bad: the regtest target
        # is so loose that a random header meets it roughly half the time, so
        # an unchecked header makes the negative test pass only by luck.
        if meets == solve:
            return hdr, nonce
    raise RuntimeError("no parent %s within %d attempts"
                       % ("solution" if solve else "failing header", limit))


def serialize_auxpow(cb, hdr, n_index=0, chain_index=0):
    """CAuxPow wire format: tx, hashBlock, vMerkleBranch, nIndex,
    vChainMerkleBranch, nChainIndex, parentBlock.

    Both merkle branches are empty: the coinbase is the only parent tx, and an
    empty chain branch means the committed root IS the aux hash (and
    getExpectedIndex mod 1 is always 0).
    """
    return (cb + sha256d(hdr) + varint(0) + struct.pack("<i", n_index)
            + varint(0) + struct.pack("<i", chain_index) + hdr)


def target_of(aux):
    """createauxblock returns HexStr of the uint256 -- little-endian order."""
    return int.from_bytes(bytes.fromhex(aux["target"]), "little")


def default_conf():
    return os.environ.get(
        "DOBBS_REGTEST_CONF",
        os.path.expanduser("~/.dobbscoin-regtest/dobbscoin.conf"))
