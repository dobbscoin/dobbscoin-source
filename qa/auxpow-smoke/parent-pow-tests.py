#!/usr/bin/env python3
"""End-to-end AuxPoW tests for (BOB), including real parent proof of work.

regtest-smoke.sh covers the createauxblock/submitauxblock RPC contract but
stops short of building a CAuxPow that satisfies the parent-PoW check, on the
assumption that doing so needs a real miner. On regtest it does not: the
target is easy enough to solve in a few scrypt attempts, so the whole path is
testable here rather than only in a MiningCore runbook.

Two halves:

  * one valid merge-mined block, which must be ACCEPTED;
  * eight proofs each mutating exactly one field, which must all be REJECTED.

The negative half is the half that matters. A fork is not safe because it
accepts good blocks, it is safe because it refuses bad ones -- and each
rejection is additionally checked against the validator error it was supposed
to trigger, so a test cannot pass for the wrong reason.

Usage:  ./parent-pow-tests.py [path/to/dobbscoin.conf] [path/to/debug.log]
        (default conf ~/.dobbscoin-regtest/dobbscoin.conf, or
        DOBBS_REGTEST_CONF; debug.log is inferred from the conf location)

Requires a running regtest daemon whose tip is at or past
HARDFORK_AUXPOW_TESTNET (10). run-parent-pow-tests.sh does that setup.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from auxpowlib import (CHAIN_ID, MAGIC, Rpc, coinbase, commitment, default_conf,
                       serialize_auxpow, sha256d, solve_parent, target_of)

CONF = sys.argv[1] if len(sys.argv) > 1 else default_conf()
LOG = sys.argv[2] if len(sys.argv) > 2 else os.path.join(
    os.path.dirname(CONF), "regtest", "debug.log")
rpc = Rpc(CONF)


def log_size():
    try:
        return os.path.getsize(LOG)
    except OSError:
        return None


def log_since(pos):
    if pos is None:
        return ""
    try:
        with open(LOG, "rb") as f:
            f.seek(pos)
            return f.read().decode("utf-8", "replace")
    except OSError:
        return ""


# ---- the eight mutations -------------------------------------------------
# Each takes the aux block hash and returns a parent coinbase scriptSig.

def script_valid(h):
    return commitment(h)

def script_no_commitment(h):
    return b"\x51" * 40                      # OP_1 padding, no root anywhere

def script_bad_tree_size(h):
    return commitment(h, tree_size=4)        # claims 4 leaves, branch is empty

def script_two_headers(h):
    return commitment(h, extra_magic=True)   # fabe6d6d twice

def script_truncated(h):
    return commitment(h, truncate=True)      # tree size present, nonce missing

def script_wrong_root(h):
    return commitment(h, root_override=bytes(32))   # commits to the wrong block


def build(script_fn=script_valid, solve=True, parent_version=1, n_index=0):
    aux = rpc("createauxblock", rpc("getnewaddress"))
    if aux["chainid"] != CHAIN_ID:
        raise AssertionError("chainid %d != %d" % (aux["chainid"], CHAIN_ID))
    cb = coinbase(script_fn(aux["hash"]))
    hdr, _ = solve_parent(aux["previousblockhash"], sha256d(cb),
                          int(aux["bits"], 16), target_of(aux),
                          version=parent_version, solve=solve)
    return aux, serialize_auxpow(cb, hdr, n_index=n_index).hex()


def case(name, expect_reject, expect_error=None, **kw):
    pos = log_size()
    before = rpc("getblockcount")
    try:
        aux, payload = build(**kw)
        result = rpc("submitauxblock", aux["hash"], payload)
        accepted = bool(result) and rpc("getblockcount") == before + 1
        detail = "returned %s" % result
    except RuntimeError as e:
        accepted, detail = False, "rpc error: %s" % str(e)[:60]

    ok = (not accepted) if expect_reject else accepted
    # A rejection must come from the check it was meant to trip, not from some
    # unrelated failure that also happens to say no.
    if ok and expect_reject and expect_error and log_size() is not None:
        if expect_error not in log_since(pos):
            ok, detail = False, "rejected, but not via %r" % expect_error
    print("  [%s] %-38s %s" % ("PASS" if ok else "FAIL", name, detail))
    return ok


def main():
    tip = rpc("getblockcount")
    print("(BOB) AuxPoW parent-PoW tests -- regtest tip %d" % tip)
    if tip < 10:
        sys.exit("tip %d is below HARDFORK_AUXPOW_TESTNET (10); "
                 "generate more blocks first" % tip)
    if log_size() is None:
        print("  note: %s unreadable -- rejection reasons will NOT be verified" % LOG)

    r = []
    print("\n  a valid merge-mined block must be accepted")
    r.append(case("valid auxpow", False))

    print("\n  malformed proofs must be refused, each for its own reason")
    r.append(case("parent claims our chain id", True,
                  "AuxPow parent has our chain ID",
                  parent_version=1 | (CHAIN_ID << 16)))
    r.append(case("no commitment in parent coinbase", True,
                  "AuxPow missing chain merkle root",
                  script_fn=script_no_commitment))
    r.append(case("tree size does not match branch", True,
                  "AuxPow merkle branch size does not match",
                  script_fn=script_bad_tree_size))
    r.append(case("two merged-mining headers", True,
                  "Multiple merged mining headers",
                  script_fn=script_two_headers))
    r.append(case("parent PoW below target", True,
                  "proof of work failed",
                  solve=False))
    r.append(case("coinbase not at index 0", True,
                  "AuxPow is not a generate",
                  n_index=1))
    r.append(case("commitment truncated, nonce absent", True,
                  "AuxPow missing chain merkle tree size and nonce",
                  script_fn=script_truncated))
    r.append(case("commits to a different aux hash", True,
                  "AuxPow missing chain merkle root",
                  script_fn=script_wrong_root))

    print("\n%d/%d passed" % (sum(r), len(r)))
    return 0 if all(r) else 1


if __name__ == "__main__":
    sys.exit(main())
