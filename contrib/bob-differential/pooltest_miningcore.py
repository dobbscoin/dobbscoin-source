#!/usr/bin/env python3
"""Live-pool MiningCore against (BOB) regtest, across the AuxPoW fork (height 10).

Two daemons on one regtest, connected by P2P: the (BOB) 0.14.1 reference and the
0.21 rebase. MiningCore (the live pool's build, unchanged) runs against ONE of them
(`--pool-on rebase|old`); a minimal stratum miner finds blocks through it. The other
daemon must follow every pool-found block over P2P, and every block's version must
carry chain ID 0 before the fork and 0x00B0 from it.

usage: pooltest.py --pool-on rebase|old --blocks N <basedir> <workdir>
  basedir holds dogecoin-121dev/src, ref-0.14.1/, pooltest/{dotnet5,miningcore/build}
"""
import argparse, base64, hashlib, json, os, socket, struct, subprocess, sys, threading, time, urllib.request

ap = argparse.ArgumentParser()
ap.add_argument("--pool-on", choices=["rebase", "old"], required=True)
ap.add_argument("--blocks", type=int, default=25)
ap.add_argument("--miner", choices=["python", "node"], default="python",
                help="python: the minimal stratum miner below; node: the rebase node's own pool client (setstratum)")
ap.add_argument("--rebase-bin", default=None, help="dobbscoind of the rebase (default: <basedir>/dogecoin-121dev/src/dobbscoind)")
ap.add_argument("--segwit", type=int, default=0,
                help="SegWit+CSV activation height on the rebase node (0 = off). The pool starts 5 blocks before it, "
                     "and a wallet keeps bech32 payments and witness spends in the mempool from the height on.")
ap.add_argument("--payments", action="store_true",
                help="also run MiningCore's payout side as the live pool does (PROP, minimum 0.5, a Postgres database "
                     "'miningcore' on 127.0.0.1 with the stock schema, user miningcore / password mcpooltest): the pool "
                     "node's wallet owns the pool address, the miner is paid to an address of the OTHER node's wallet")
ap.add_argument("base"); ap.add_argument("work")
A = ap.parse_args()
BASE, WORK = os.path.abspath(A.base), os.path.abspath(A.work)
AUXPOW_FORK, BOB = 10, 0x00B0
STRATUM_PORT = 3933


POOL_ADDR = None   # regtest P2PKH (prefix 111), derived by the rebase node at start
FEE_ADDR = None    # --payments: the 1% reward recipient of the pool itself
MINER_ADDR = None  # --payments: where the miner is paid (an address of the other node's wallet)
PG = {"host": "127.0.0.1", "port": 5432, "user": "miningcore", "password": "mcpooltest", "database": "miningcore"}


def psql(sql):
    out = subprocess.run(["psql", "-h", PG["host"], "-U", PG["user"], "-d", PG["database"], "-At", "-c", sql],
                         env=dict(os.environ, PGPASSWORD=PG["password"]), capture_output=True, text=True)
    if out.returncode:
        raise RuntimeError("psql: " + out.stderr.strip()[:200])
    return out.stdout.strip()


class Node:
    def __init__(self, name, argv, port, env=None):
        self.name, self.port = name, port
        dd = os.path.join(WORK, name); os.makedirs(dd, exist_ok=True)
        self.proc = subprocess.Popen(argv + ["-regtest", "-datadir=" + dd, "-rpcuser=d", "-rpcpassword=d",
                                             "-rpcport=%d" % port, "-server=1", "-printtoconsole=0"],
                                     stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                     stderr=subprocess.DEVNULL, env=env)
        for _ in range(120):
            try:
                self.call("getblockcount"); return
            except Exception:
                time.sleep(0.5)
        raise RuntimeError(name + " RPC never came up")

    def call(self, method, *params):
        req = urllib.request.Request("http://127.0.0.1:%d/" % self.port,
                                     json.dumps({"id": 1, "method": method, "params": list(params)}).encode(),
                                     {"Authorization": "Basic " + base64.b64encode(b"d:d").decode()})
        try:
            r = json.load(urllib.request.urlopen(req, timeout=60))
        except urllib.error.HTTPError as e:
            r = json.load(e)
        if r.get("error"):
            raise RuntimeError("%s %s: %s" % (self.name, method, r["error"]))
        return r["result"]

    def stop(self):
        try:
            self.call("stop"); self.proc.wait(60)
        except Exception:
            self.proc.kill()


def sha256d(b):
    return hashlib.sha256(hashlib.sha256(b).digest()).digest()


def scrypt_int(header):
    return int.from_bytes(hashlib.scrypt(header, salt=header, n=1024, r=1, p=1, dklen=32), "little")


def target_from_bits(bits):
    e, m = bits >> 24, bits & 0xffffff
    return m << (8 * (e - 3))


class Miner(threading.Thread):
    """Minimal stratum v1 client: submits only nonces that meet the BLOCK target."""
    def __init__(self):
        super().__init__(daemon=True)
        self.job, self.lock, self.submits, self.results, self.errors = None, threading.Lock(), 0, [], []
        self.stop_flag = False

    def send(self, i, method, params):
        self.f.write((json.dumps({"id": i, "method": method, "params": params}) + "\n").encode()); self.f.flush()

    def reader(self):
        for line in self.f:
            msg = json.loads(line)
            if msg.get("method") == "mining.notify":
                with self.lock:
                    self.job = msg["params"]
            elif msg.get("id") == 1:
                self.en1, self.en2size = msg["result"][1], msg["result"][2]
            elif (msg.get("id") or 0) >= 100:      # notifications carry "id": null
                self.results.append((msg.get("result"), msg.get("error")))

    def run(self):
        for _ in range(120):
            try:
                self.sock = socket.create_connection(("127.0.0.1", STRATUM_PORT)); break
            except OSError:
                time.sleep(1)
        self.f = self.sock.makefile("rwb")
        threading.Thread(target=self.reader, daemon=True).start()
        self.send(1, "mining.subscribe", ["pooltest/1.0"])
        self.send(2, "mining.authorize", [POOL_ADDR + ".test", "x"])
        sid, last = 100, None
        while not self.stop_flag:
            with self.lock:
                job = self.job
            if not job or not hasattr(self, "en1") or job is last:
                time.sleep(0.05); continue
            last = job
            jid, prevhex, cb1, cb2, branch, verhex, bitshex, timehex = job[:8]
            en2 = os.urandom(self.en2size).hex()
            root = sha256d(bytes.fromhex(cb1 + self.en1 + en2 + cb2))
            for b in branch:
                root = sha256d(root + bytes.fromhex(b))
            prev = b"".join(bytes.fromhex(prevhex)[i:i + 4][::-1] for i in range(0, 32, 4))
            head = (struct.pack("<I", int(verhex, 16)) + prev + root +
                    struct.pack("<II", int(timehex, 16), int(bitshex, 16)))
            target = target_from_bits(int(bitshex, 16))
            for nonce in range(0, 1 << 32):
                if self.job is not job:
                    break
                if scrypt_int(head + struct.pack("<I", nonce)) <= target:
                    self.send(sid, "mining.submit", [(MINER_ADDR or POOL_ADDR) + ".test", jid, en2, timehex, "%08x" % nonce])
                    sid += 1; self.submits += 1
                    break


def main():
    os.makedirs(WORK, exist_ok=True)
    refenv = dict(os.environ, LD_LIBRARY_PATH=os.path.join(BASE, "ref-0.14.1", "lib"))
    old = Node("old-0.14.1", [os.path.join(BASE, "ref-0.14.1", "dobbscoind"), "-port=19556", "-listen=1",
                              "-bind=127.0.0.1"], 19555, refenv)
    new = Node("rebase", [A.rebase_bin or os.path.join(BASE, "dogecoin-121dev", "src", "dobbscoind"), "-port=19566", "-listen=0",
                          "-connect=127.0.0.1:19556"] + (["-segwitheight=%d" % A.segwit, "-csvheight=%d" % A.segwit]
                                                         if A.segwit else [] if A.payments else ["-disablewallet"]), 19565)
    nodes = {"old": old, "rebase": new}
    pool_node, follower = nodes[A.pool_on], nodes["old" if A.pool_on == "rebase" else "rebase"]
    mc = None
    try:
        for _ in range(60):
            if new.call("getconnectioncount") > 0:
                break
            time.sleep(0.5)
        assert new.call("getconnectioncount") > 0, "rebase never connected to 0.14 over P2P"
        global POOL_ADDR
        pk = "02" + "22" * 32
        POOL_ADDR = new.call("deriveaddresses", new.call("getdescriptorinfo", "pkh(%s)" % pk)["descriptor"])[0]
        assert old.call("validateaddress", POOL_ADDR)["isvalid"], "0.14 rejects the pool address"
        if A.payments:
            global MINER_ADDR, FEE_ADDR
            if not A.segwit:
                new.call("createwallet", "pooltest")
            POOL_ADDR = pool_node.call("getnewaddress")       # the pool node's own wallet must hold the rewards
            MINER_ADDR = follower.call("getnewaddress")
            FEE_ADDR = pool_node.call("getnewaddress")
            psql("TRUNCATE shares, blocks, balances, balance_changes, payments, minerstats, poolstats")
        # One block with a current timestamp ends initial block download on both.
        desc = new.call("getdescriptorinfo", "raw(51)")["descriptor"]
        new.call("generatetodescriptor", 1, desc)
        feeder_stop = threading.Event()
        if A.segwit:
            # mature wallet coins, and stop 5 blocks short so the POOL mines across the activation
            new.call("createwallet", "w")
            wa = new.call("getnewaddress", "", "legacy")
            new.call("generatetoaddress", A.segwit - 5 - new.call("getblockcount"), wa)
            def feeder():
                while not feeder_stop.is_set():
                    try:
                        if new.call("getblockcount") >= A.segwit:
                            for _ in range(2):
                                new.call("sendtoaddress", new.call("getnewaddress", "", "bech32"), 1)
                            # and spend witness outputs (unconfirmed ones too: the pool mines a block a
                            # second here), so blocks carry witness inputs
                            for u in [u for u in new.call("listunspent", 0) if u.get("desc", "").startswith("wpkh(")][:4]:
                                raw = new.call("createrawtransaction", [{"txid": u["txid"], "vout": u["vout"]}],
                                               {new.call("getnewaddress", "", "bech32"): float(u["amount"]) - 0.001})
                                new.call("sendrawtransaction", new.call("signrawtransactionwithwallet", raw)["hex"])
                    except Exception as e:
                        print("   feeder:", str(e)[:100])
                    feeder_stop.wait(0.2)
            threading.Thread(target=feeder, daemon=True).start()
        time.sleep(2)
        for _ in range(60):
            if old.call("getblockcount") == new.call("getblockcount"): break
            time.sleep(1)
        assert old.call("getblockcount") == new.call("getblockcount"), "0.14.1 did not follow the setup blocks"

        cfg = {
            "logging": {"level": "info", "enableConsoleLog": True, "enableConsoleColors": False,
                        "logFile": "", "logBaseDirectory": "", "perPoolLogFile": False},
            "banning": {"manager": "integrated", "banOnJunkReceive": True, "banOnInvalidShares": False},
            "notifications": {"enabled": False},
            "persistence": {"postgres": PG} if A.payments else {},
            "paymentProcessing": {"enabled": A.payments, "interval": 15 if A.payments else 600, "shareRecoveryFile": "recovered-shares.txt"},
            "pools": [{
                "id": "dobbscoin", "enabled": True, "coin": "dobbscoin", "address": POOL_ADDR,
                # the live pool keeps a fee through one reward recipient; without one the wallet owes every
                # coin it holds and has nothing left for the fee of the payout itself ("Insufficient funds")
                "rewardRecipients": ([{"address": FEE_ADDR, "percentage": 1.0}] if A.payments else []), "blockRefreshInterval": 250, "jobRebroadcastTimeout": 10,
                "clientConnectionTimeout": 600,
                "banning": {"enabled": False},
                "ports": {str(STRATUM_PORT): {"listenAddress": "127.0.0.1", "difficulty": 0.0001, "name": "pooltest"}},
                "daemons": [{"host": "127.0.0.1", "port": pool_node.port, "user": "d", "password": "d"}],
                "paymentProcessing": ({"enabled": True, "minimumPayment": 0.5, "payoutScheme": "PROP",
                                       "payoutSchemeConfig": {"factor": 2.0}} if A.payments else {"enabled": False}),
            }],
        }
        cfgpath = os.path.join(WORK, "miningcore.json")
        json.dump(cfg, open(cfgpath, "w"), indent=1)
        mcdir = os.path.join(BASE, "pooltest", "miningcore", "build")
        mclog = open(os.path.join(WORK, "miningcore.log"), "w")
        mc = subprocess.Popen([os.path.join(BASE, "pooltest", "dotnet5", "dotnet"), "Miningcore.dll", "-c", cfgpath],
                              cwd=mcdir, stdin=subprocess.DEVNULL, stdout=mclog, stderr=subprocess.STDOUT,
                              env=dict(os.environ, DOTNET_SYSTEM_GLOBALIZATION_INVARIANT="1",
                                       LD_LIBRARY_PATH=os.path.join(BASE, "pooltest", "lib")))
        if A.miner == "node":
            time.sleep(5)  # let MiningCore open its stratum port
            new.call("setstratum", True, "127.0.0.1:%d" % STRATUM_PORT, POOL_ADDR + ".node", 2)
        else:
            miner = Miner(); miner.start()

        start_h, deadline = pool_node.call("getblockcount"), time.time() + 900
        target_h = start_h + A.blocks
        while pool_node.call("getblockcount") < target_h and time.time() < deadline:
            if mc.poll() is not None:
                raise SystemExit("MiningCore exited (%s); see miningcore.log" % mc.returncode)
            time.sleep(1)
        if A.miner == "node":
            info = new.call("getstratuminfo")
            new.call("setstratum", False)
            print("node pool client:", {k: info.get(k) for k in ("authorized", "sharedifficulty", "sharessubmitted", "sharesaccepted", "sharesrejected", "errors")})
        else:
            miner.stop_flag = True
        h = pool_node.call("getblockcount")
        print("pool on %s: height %d -> %d (%s)" % (pool_node.name, start_h, h, "node client" if A.miner == "node" else "%d submits" % miner.submits))
        if h < target_h:
            raise SystemExit("FAIL: pool only reached %d of %d" % (h, target_h))
        time.sleep(3)

        # Every pool-found block: version / chain ID per the fork, followed over P2P.
        bad = 0
        for height in range(start_h + 1, h + 1):
            bh = pool_node.call("getblockhash", height)
            blk = pool_node.call("getblock", bh)
            ver = blk["version"] if isinstance(blk["version"], int) else int(blk["versionHex"], 16)
            chain = ver >> 16
            want = BOB if height >= AUXPOW_FORK else 0
            fh = follower.call("getblockhash", height) if follower.call("getblockcount") >= height else None
            ok = chain == want and fh == bh and not ver & 0x100
            bad += not ok
            print("%s h=%-3d %s ver=0x%08x chain=0x%04x %s" % ("  " if ok else "!!", height, bh[:16], ver & 0xffffffff,
                                                            chain, "followed" if fh == bh else "NOT FOLLOWED (%s)" % fh))
            if fh != bh:      # get the follower's verdict directly
                try:
                    print("     follower submitblock ->", follower.call("submitblock", pool_node.call("getblock", bh, False)))
                except RuntimeError as e:
                    print("     follower submitblock ->", e)
        log = open(os.path.join(WORK, "miningcore.log")).read()
        found = log.count("Daemon accepted block")
        rejected = [l for l in log.splitlines() if "reject" in l.lower() or "invalid" in l.lower()]
        print("miningcore: %d 'Daemon accepted block' lines, %d reject/invalid lines" % (found, len(rejected)))
        for l in rejected[:10]:
            print("   ", l[:200])
        if A.miner == "node":
            bad += info.get("sharesrejected", 0) != 0 or info.get("sharesaccepted", 0) == 0
        else:
            print("stratum results:", sum(1 for r, e in miner.results if r is True), "accepted,",
                  sum(1 for r, e in miner.results if r is not True), "not accepted",
                  [e for r, e in miner.results if r is not True][:5])
        bad += found != h - start_h          # MiningCore must have seen every block accepted
        if A.segwit:
            feeder_stop.set()
            wit = commit = after = 0
            for height in range(max(start_h + 1, A.segwit), h + 1):
                blk = pool_node.call("getblock", pool_node.call("getblockhash", height), 2)
                after += 1
                wit += sum(1 for tx in blk["tx"][1:] for vin in tx["vin"] if vin.get("txinwitness"))
                commit += any(o["scriptPubKey"]["hex"].startswith("6a24aa21a9ed") for o in blk["tx"][0]["vout"])
            print("segwit: %d pool blocks from the activation height, %d witness inputs in them, %d with a witness commitment"
                  % (after, wit, commit))
            bad += wit == 0
        if A.payments:
            # Let the pool's blocks mature (100), mined outside the pool to the OTHER node's wallet,
            # then give the payout loop (every 15 s) time to confirm the blocks and pay the miner.
            print("== payouts: maturing the pool's blocks, then waiting for MiningCore to pay", MINER_ADDR)
            spare = follower.call("getnewaddress")
            if pool_node is new:
                # five blocks of its own first: the wallet owes every pool coin it holds (miner + fee recipient),
                # so it needs something else to pay the network fee of the payout from. The 0.14 side gets the
                # same from setgenerate, which mines to its own wallet.
                new.call("generatetoaddress", 5, new.call("getnewaddress"))
                new.call("generatetoaddress", 110, spare)
            else:
                old.call("setgenerate", True, 110)
            paid, deadline = "", time.time() + 240
            while time.time() < deadline:
                if mc.poll() is not None:
                    raise SystemExit("MiningCore exited (%s); see miningcore.log" % mc.returncode)
                paid = psql("select count(*) || ' ' || coalesce(sum(amount),0) || ' ' || coalesce(max(transactionconfirmationdata),'') from payments")
                if not paid.startswith("0 "):
                    break
                time.sleep(5)
            log = open(os.path.join(WORK, "miningcore.log")).read()
            for l in [l for l in log.splitlines() if "ayout" in l or "ayment" in l or "ismine" in l.lower() or "[E]" in l or "rror" in l][-14:]:
                print("    mc:", l[:210])
            print("blocks table:", psql("select status || ':' || count(*) from blocks group by status").replace("\n", " "))
            print("payments table (count, sum, txid):", paid)
            n_pay = int(paid.split()[0]) if paid else 0
            bad += n_pay == 0
            if n_pay:
                txid = paid.split()[2]
                tx = pool_node.call("gettransaction", txid)
                print("payout tx on the pool node: %s, amount %s, fee %s, confirmations %s" % (txid[:16], tx["amount"], tx.get("fee"), tx["confirmations"]))
                time.sleep(3)
                got = follower.call("getreceivedbyaddress", MINER_ADDR, 0)
                print("miner address %s received (0-conf, seen by the other node over P2P): %s" % (MINER_ADDR, got))
                bad += not float(got) >= 0.5
                (new.call("generatetoaddress", 1, spare) if pool_node is new else old.call("setgenerate", True, 1))
                time.sleep(3)
                got1 = follower.call("getreceivedbyaddress", MINER_ADDR, 1)
                print("after one more block, confirmed on the other node: %s" % got1)
                bad += not float(got1) >= 0.5
        print("PASS" if bad == 0 else "FAIL: %d problem(s)" % bad)
        return 0 if bad == 0 else 1
    finally:
        if mc and mc.poll() is None:
            mc.terminate()
            try:
                mc.wait(20)
            except subprocess.TimeoutExpired:
                mc.kill()
        old.stop(); new.stop()


if __name__ == "__main__":
    sys.exit(main())
