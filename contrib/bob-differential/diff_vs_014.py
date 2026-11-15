#!/usr/bin/env python3
"""Differential consensus test: (BOB) 0.14 daemon vs the Dogecoin-Core-0.21 rebase.

Both daemons run the same (BOB) regtest (same genesis, AuxPoW fork at 10, CLTV at
150). Every probe block is built once and submitted to BOTH; the 0.14 verdict is
the reference. Blocks mined by each daemon's own miner (0.14 setgenerate /
createauxblock+submitauxblock, rebase generatetodescriptor) are fed to the other.
At the end both chains must be identical block for block.

usage: diff_vs_014.py <rebase-src-dir> <ref-0.14-dir> <workdir>
(BOB_TEST_FUNCTIONAL=<dir> if the binaries are not in a source tree)
"""
import base64, hashlib, json, os, struct, subprocess, sys, time, urllib.request

REBASE, REF, WORK = (os.path.abspath(a) for a in sys.argv[1:4])
sys.path.insert(0, os.environ.get("BOB_TEST_FUNCTIONAL") or
                os.path.join(REBASE, "..", "test", "functional"))
from test_framework.blocktools import create_coinbase, create_block            # noqa: E402
from test_framework.messages import (CAuxPow, CBlock, CBlockHeader, COutPoint,  # noqa: E402
                                     CTransaction, CTxIn, CTxOut, FromHex,
                                     VERSION_AUXPOW, VERSION_CHAIN_START,
                                     ser_uint256, uint256_from_compact)
from test_framework.script import (CScript, CScriptNum, OP_1NEGATE,            # noqa: E402
                                   OP_CHECKLOCKTIMEVERIFY, OP_DROP, OP_TRUE)

BOB, DOGE = 0x00B0, 0x0062
AUXPOW_FORK, CLTV_FORK = 10, 150
MAGIC = b'\xfa\xbe' + b'mm'
GENESIS = "63c57ac1f1acc24a6ad8f9e938237f42370ad80a3fba52b818c35140db98a07f"


def ver(base, chain=0, aux=False):
    return base + chain * VERSION_CHAIN_START + (VERSION_AUXPOW if aux else 0)


class Node:
    def __init__(self, name, argv, port, env=None):
        self.name, self.port = name, port
        dd = os.path.join(WORK, name)
        os.makedirs(dd, exist_ok=True)
        self.log = open(os.path.join(WORK, name + ".stdout"), "w")
        self.proc = subprocess.Popen(argv + ["-regtest", "-datadir=" + dd, "-rpcuser=d", "-rpcpassword=d",
                                             "-rpcport=%d" % port, "-port=%d" % (port + 1), "-listen=0",
                                             "-server=1", "-printtoconsole=0"],
                                     stdin=subprocess.DEVNULL, stdout=self.log, stderr=self.log, env=env)
        for _ in range(120):
            try:
                self.call("getblockcount"); return
            except Exception:
                time.sleep(0.5)
        raise RuntimeError(name + " RPC never came up")

    def call(self, method, *params):
        req = urllib.request.Request("http://127.0.0.1:%d/" % self.port,
                                     json.dumps({"id": 1, "method": method, "params": list(params)}).encode(),
                                     {"Authorization": "Basic " + base64.b64encode(b"d:d").decode(),
                                      "Content-Type": "application/json"})
        try:
            r = json.load(urllib.request.urlopen(req, timeout=120))
        except urllib.error.HTTPError as e:
            r = json.load(e)
        if r.get("error"):
            raise RuntimeError("%s %s: %s" % (self.name, method, r["error"]))
        return r["result"]

    def tip(self):
        return self.call("getbestblockhash")

    def block_hex(self, h):
        return self.call("getblock", h, False)

    def stop(self):
        try:
            self.call("stop")
            self.proc.wait(60)
        except Exception:
            self.proc.kill()


class Diff:
    def __init__(self):
        env = dict(os.environ, LD_LIBRARY_PATH=os.path.join(REF, "lib"))
        self.old = Node("old-0.14.1", [os.path.join(REF, "dobbscoind")], 19555, env)
        self.new = Node("rebase", [os.path.join(REBASE, "dogecoind"), "-disablewallet"], 19565)
        self.rows, self.divergences, self.coinbases = [], [], []
        g = (self.old.call("getblockhash", 0), self.new.call("getblockhash", 0))
        assert g == (GENESIS, GENESIS), "genesis differs: %s" % (g,)
        self.time = int(time.time()) - 3600
        self.addr = self.old.call("getnewaddress")
        info = self.new.call("getdescriptorinfo", "raw(51)")
        self.desc = "raw(51)#" + info["checksum"]

    # -------------------------------------------------------------- plumbing
    @property
    def height(self):
        return self.old.call("getblockcount") + 1

    def sync_check(self, where):
        a, b = self.old.tip(), self.new.tip()
        if a != b:
            raise SystemExit("FATAL tips differ %s: old %s new %s" % (where, a, b))

    def submit_both(self, block, label, expect):
        """Submit to both; compare verdicts; on disagreement roll the acceptor back."""
        hexb = block.serialize().hex()
        h = self.height
        verdict = {}
        for n in (self.old, self.new):
            try:
                res = n.call("submitblock", hexb)
            except RuntimeError as e:
                res = "RPC-ERROR " + str(e)[:80]
            verdict[n.name] = (n.tip() == block.hash, res)
        o, nw = verdict[self.old.name], verdict[self.new.name]
        same = o[0] == nw[0]
        self.rows.append((h, label, expect, o, nw, same))
        flag = "  " if same else "!!"
        print("%s h=%-4d %-58s old=%s(%s) new=%s(%s)" % (flag, h, label[:58], "ACC" if o[0] else "rej", o[1],
                                                       "ACC" if nw[0] else "rej", nw[1]), flush=True)
        if not same:
            self.divergences.append((h, label, o, nw))
            for n, (acc, _) in ((self.old, o), (self.new, nw)):
                if acc:
                    n.call("invalidateblock", block.hash)
        elif o[0]:
            self.time += 1
            if block.vtx[0].vout[0].scriptPubKey == CScript([OP_TRUE]):
                self.coinbases.append((h, block.vtx[0]))
        self.sync_check("after " + label)
        return o[0]

    def feed(self, src, dst, hashes, label):
        for bh in hashes:
            h = src.call("getblock", bh)["height"]
            res = dst.call("submitblock", src.block_hex(bh))
            acc = dst.tip() == bh
            self.rows.append((h, label, "ACC", (True, None) if src is self.old else (acc, res),
                              (acc, res) if src is self.old else (True, None), acc))
            print("%s h=%-4d %-58s %s -> %s: %s(%s)" % ("  " if acc else "!!", h, label[:58], src.name, dst.name,
                                                       "ACC" if acc else "rej", res), flush=True)
            if not acc:
                self.divergences.append((h, label + " " + bh, src.name, res))
                raise SystemExit("FATAL: %s rejected a block %s mined (%s)" % (dst.name, src.name, res))
        self.time = max(self.time, int(time.time())) + 1
        self.sync_check(label)

    def old_mines(self, n, label="0.14 miner (setgenerate) block"):
        hashes = self.old.call("setgenerate", True, n)
        self.feed(self.old, self.new, hashes, label)

    def new_mines(self, n, label="rebase miner (generatetodescriptor) block"):
        hashes = self.new.call("generatetodescriptor", n, self.desc)
        self.feed(self.new, self.old, hashes, label)

    # -------------------------------------------------------------- builders
    def block(self, nversion, bump=0, txs=()):
        cb = create_coinbase(self.height)
        cb.vout[0].nValue = 100000000
        cb.rehash()
        tip = int(self.old.tip(), 16)
        t = max(self.time, self.old.call("getblock", self.old.tip())["time"] + 1) + bump
        b = create_block(tip, cb, t, txlist=list(txs))
        b.nVersion = nversion
        b.hashMerkleRoot = b.calc_merkle_root()
        b.rehash()
        return b

    def solved(self, *a, **k):
        b = self.block(*a, **k)
        b.solve()
        return b

    def grind_own(self, b, valid):
        target = uint256_from_compact(b.nBits)
        b.rehash()
        while (b.scrypt256 <= target) != valid:
            b.nNonce += 1
            b.rehash()

    @staticmethod
    def expected_index(nonce, chain, h):
        r = (nonce * 1103515245 + 12345) & 0xffffffff
        r = (r + chain) & 0xffffffff
        r = (r * 1103515245 + 12345) & 0xffffffff
        return r % (1 << h)

    def auxpow(self, b, **kw):
        b.rehash()
        b.auxpow = self.make_auxpow(b.sha256, b.nTime, b.nBits, **kw)
        return b

    def make_auxpow(self, block_hash, ntime, nbits, *, parent_valid=True, parent_chain=0, n_index=0, root_xor=0,
               magic="adjacent", chain_branch=0, chain_index=None, size=None, tree_nonce=7,
               parent_sibling=False):
        """Commit block_hash into a synthetic parent coinbase and grind the parent."""
        leaf, branch = block_hash, []
        h = chain_branch
        idx = self.expected_index(tree_nonce, BOB, h) if chain_index is None else chain_index
        node, pos = leaf, idx
        for i in range(h):                                   # build a real chain merkle path
            sib = 0x1111 * (i + 1)
            branch.append(sib)
            pair = (ser_uint256(sib) + ser_uint256(node)) if pos & 1 else (ser_uint256(node) + ser_uint256(sib))
            node = int.from_bytes(hashlib.sha256(hashlib.sha256(pair).digest()).digest(), "little")
            pos >>= 1
        root = node ^ root_xor
        tail = ser_uint256(root)[::-1] + struct.pack("<II", (1 << h) if size is None else size, tree_nonce)
        if magic == "adjacent":
            payload = MAGIC + tail
        elif magic == "twice":
            payload = MAGIC + tail + MAGIC
        elif magic == "gap":
            payload = MAGIC + b"\x00" + tail
        elif magic == "none-early":
            payload = b"\x01\x02" + tail
        elif magic == "none-late":
            payload = b"\x00" * 24 + tail
        cbtx = CTransaction()
        cbtx.vin = [CTxIn(COutPoint(0, 0xffffffff), CScript([payload]), 0xffffffff)]
        cbtx.vout = [CTxOut(0, CScript([OP_TRUE]))]
        cbtx.rehash()
        ap = CAuxPow()
        ap.nVersion, ap.vin, ap.vout, ap.nLockTime = cbtx.nVersion, cbtx.vin, cbtx.vout, cbtx.nLockTime
        ap.hashBlock, ap.nIndex = 0, n_index
        ap.vChainMerkleBranch, ap.nChainIndex = branch, idx
        mroot = cbtx.sha256
        if parent_sibling:                                    # parent tree of 2: [coinbase, other]
            sib = 0xabcdef
            ap.vMerkleBranch = [sib]
            pair = ser_uint256(cbtx.sha256) + ser_uint256(sib)
            mroot = int.from_bytes(hashlib.sha256(hashlib.sha256(pair).digest()).digest(), "little")
        else:
            ap.vMerkleBranch = []
        p = CBlockHeader()
        p.nVersion = ver(2, parent_chain)
        p.hashPrevBlock, p.hashMerkleRoot, p.nTime, p.nBits, p.nNonce = 0, mroot, ntime, nbits, 0
        target = uint256_from_compact(p.nBits)
        p.rehash()
        while (p.scrypt256 <= target) != parent_valid:
            p.nNonce += 1
            p.rehash()
        ap.parentBlock = p
        return ap

    def mm(self, bump, own_valid=False, base=3, **kw):
        b = self.block(ver(base, BOB, aux=True), bump)
        self.grind_own(b, own_valid)
        return self.auxpow(b, **kw)

    def spend(self, cb, prefix, nseq=0xffffffff, nlock=0):
        tx = CTransaction()
        tx.vin = [CTxIn(COutPoint(cb.sha256, 0), CScript(prefix), nseq)]
        tx.vout = [CTxOut(cb.vout[0].nValue - 1000, CScript([OP_TRUE]))]
        tx.nLockTime = nlock
        tx.rehash()
        return tx

    def mature_coinbase(self):
        for i, (h, cb) in enumerate(self.coinbases):
            if self.height - h >= 100:
                return self.coinbases.pop(i)[1]
        raise SystemExit("no mature coinbase at height %d" % self.height)

    # -------------------------------------------------------------- the run
    def run(self):
        S = self.submit_both
        print("== pre-fork (AuxPoW at %d)" % AUXPOW_FORK)
        for base in (1, 2, 3, 4):
            S(self.solved(ver(base)), "legacy base %d, chain 0" % base, "ACC")
        S(self.solved(ver(3, BOB), 1), "solo, chain 0x00B0 before fork", "rej")
        S(self.solved(ver(3, DOGE), 2), "solo, chain 0x0062 before fork", "rej")
        S(self.mm(3), "merge-mined, chain 0x00B0, before fork", "rej")
        b = self.block(ver(3, 0, aux=True), 4); self.grind_own(b, False); self.auxpow(b)
        S(b, "merge-mined, chain 0, before fork", "rej")
        self.old_mines(2, "0.14 miner block, pre-fork")
        self.new_mines(AUXPOW_FORK - self.height, "rebase miner block, pre-fork (chain 0?)")

        print("== AuxPoW fork height %d" % self.height)
        assert self.height == AUXPOW_FORK
        S(self.solved(ver(3)), "legacy chain 0 at fork", "rej")
        S(self.solved(ver(3, DOGE), 1), "chain 0x0062 at fork", "rej")
        S(self.mm(2, own_valid=True, parent_valid=False), "mm: parent misses target, own passes", "rej")
        S(self.mm(3, parent_chain=BOB), "mm: parent carries chain 0x00B0", "rej")
        S(self.mm(4, n_index=1), "mm: parent coinbase nIndex = 1", "rej")
        S(self.mm(5, root_xor=1), "mm: commits to another hash", "rej")
        S(self.mm(6, magic="twice"), "mm: merged-mining header twice", "rej")
        S(self.mm(7, magic="gap"), "mm: header not adjacent to root", "rej")
        S(self.mm(8, chain_index=1), "mm: wrong chain index (size 1)", "rej")
        S(self.mm(9, size=2), "mm: declared tree size != 1<<branch", "rej")
        S(self.mm(10, magic="none-late"), "mm: no header, root after byte 20", "rej")
        S(self.mm(11, chain_branch=31), "mm: chain branch 31 > 30", "rej")
        S(self.mm(12, chain_branch=2, chain_index=(self.expected_index(7, BOB, 2) + 1) % 4),
          "mm: branch 2, index off by one", "rej")
        S(self.mm(13), "mm: valid, own hash MISSES target", "ACC")
        S(self.mm(0, magic="none-early"), "mm: no header, root in first 20 bytes", "ACC")
        S(self.mm(0, chain_branch=2), "mm: chain merkle branch of 2", "ACC")
        S(self.mm(0, parent_sibling=True), "mm: parent tree with a sibling tx", "ACC")
        S(self.mm(0, own_valid=True), "mm: valid, own hash also meets target", "ACC")
        S(self.solved(ver(3, BOB)), "solo chain 0x00B0 base 3", "ACC")
        S(self.solved(ver(1, BOB)), "solo chain 0x00B0 base 1", "?")
        S(self.solved(ver(2, BOB)), "solo chain 0x00B0 base 2", "?")
        S(self.mm(0, base=4), "mm base 4", "ACC")

        print("== 0.14 merge-mining RPC (createauxblock/submitauxblock) -> rebase")
        for i in range(2):
            t = self.old.call("createauxblock", self.addr)
            assert t["chainid"] == BOB
            ap = self.make_auxpow(int(t["hash"], 16), int(time.time()), int(t["bits"], 16))
            ok = self.old.call("submitauxblock", t["hash"], ap.serialize().hex())
            print("   submitauxblock #%d -> %s" % (i + 1, ok))
            assert ok is True, "0.14 refused its own merge-mined block"
            self.feed(self.old, self.new, [t["hash"]], "0.14 createauxblock+submitauxblock block")

        print("== post-fork miners, up to CLTV")
        self.new_mines(3, "rebase miner block, post-fork")
        self.old_mines(CLTV_FORK - 1 - self.height, "0.14 miner block, post-fork")
        assert self.height == CLTV_FORK - 1

        print("== CLTV fork height %d" % CLTV_FORK)
        bad = [OP_1NEGATE, OP_CHECKLOCKTIMEVERIFY, OP_DROP]
        S(self.solved(ver(3, BOB), txs=[self.spend(self.mature_coinbase(), bad)]), "CLTV -1 before fork (NOP)", "ACC")
        assert self.height == CLTV_FORK
        S(self.solved(ver(3, BOB), 1, txs=[self.spend(self.mature_coinbase(), bad)]), "CLTV -1 at fork", "rej")
        lock = self.height - 1
        good = [CScriptNum(lock), OP_CHECKLOCKTIMEVERIFY, OP_DROP]
        S(self.solved(ver(3, BOB), 2, txs=[self.spend(self.mature_coinbase(), good, 0xffffffff, lock)]),
          "CLTV satisfied but input final (nSequence max)", "rej")
        S(self.solved(ver(3, BOB), 3, txs=[self.spend(self.mature_coinbase(), [CScriptNum(lock + 5), OP_CHECKLOCKTIMEVERIFY, OP_DROP], 0, lock)]),
          "CLTV lock above tx nLockTime", "rej")
        S(self.solved(ver(3, BOB), 4, txs=[self.spend(self.mature_coinbase(), [CScriptNum(500000001), OP_CHECKLOCKTIMEVERIFY, OP_DROP], 0, lock)]),
          "CLTV time-lock vs height nLockTime", "rej")
        S(self.solved(ver(3, BOB), 5, txs=[self.spend(self.mature_coinbase(), good, 0, lock)]),
          "CLTV satisfied, base-3 block", "ACC")
        S(self.mm(0, base=3), "mm base 3 after CLTV fork", "ACC")

        print("== after both forks: each miner's blocks into the other")
        self.old_mines(3, "0.14 miner block (base 3) after CLTV fork")
        self.new_mines(3, "rebase miner block after CLTV fork")

        n = self.old.call("getblockcount")
        assert n == self.new.call("getblockcount")
        for h in range(n + 1):
            a, b = self.old.call("getblockhash", h), self.new.call("getblockhash", h)
            if a != b:
                raise SystemExit("FATAL chains differ at height %d" % h)
        print("== chains identical, %d blocks, tip %s" % (n, self.old.tip()))

    def report(self):
        total = len(self.rows)
        print("\n%d checks, %d divergence(s)" % (total, len(self.divergences)))
        for d in self.divergences:
            print("  DIVERGENCE", d)
        unexpected = [r for r in self.rows if r[2] in ("ACC", "rej") and r[5]
                      and (r[3][0] != (r[2] == "ACC"))]
        for r in unexpected:
            print("  agree, but not what I predicted from 0.14 source:", r[:3], r[3])
        return 0 if not self.divergences else 1


if __name__ == "__main__":
    d = Diff()
    rc = 1
    try:
        d.run()
        rc = d.report()
    finally:
        d.old.stop(); d.new.stop()
    sys.exit(rc)
