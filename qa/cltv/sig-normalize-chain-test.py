#!/usr/bin/env python3
"""On-chain regression test for signature normalisation.

This is the case where libsecp256k1 and OpenSSL genuinely disagree, and it is
the one that would split the chain if CPubKey::Verify got it wrong.

Low-S is NOT a consensus rule on this chain -- SCRIPT_VERIFY_LOW_S lives in the
standard flags only, never in the mandatory set -- so a HIGH-S signature is
perfectly valid in a block and historical blocks may contain them. libsecp256k1
accepts only low-S signatures, so a naive port rejects those blocks while an
OpenSSL node accepts them. That is a chain split, not a stricter node.

secp256k1_ecdsa_signature_normalize() in CPubKey::Verify is what prevents it,
and this test is what proves the call is still there. Delete the normalise line
and this test fails while every unit test still passes.

Shares the block-building harness with the CLTV suite next door; see
qa/cltv/README.md for the regtest traps it encodes.

Usage: ./sig-normalize-chain-test.py <datadir>/dobbscoin.conf
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cltvlib import Key, Rpc, mine_block, spend, txid

SPACING = 600
results = []
coinbases = []
clock = [0]


def check(name, ok, detail=""):
    print("  [%s] %-42s %s" % ("PASS" if ok else "FAIL", name, detail))
    results.append(ok)


def mine(rpc, txs, key):
    clock[0] += SPACING
    rpc("setmocktime", clock[0])
    res, cb_txid, height, cb_value = mine_block(rpc, txs, key)
    if res is None:
        coinbases.append((cb_txid, cb_value))
    return res, height


def main():
    rpc = Rpc(sys.argv[1])
    key = Key()
    clock[0] = int(time.time())
    rpc("setmocktime", clock[0])

    print("(BOB) signature normalisation on-chain test")
    while rpc("getblockcount") < 101:
        res, _ = mine(rpc, [], key)
        if res is not None:
            raise RuntimeError("filler block rejected: %s" % res)
    print("  tip %d, coinbases spendable" % rpc("getblockcount"))

    # Control: an ordinary low-S spend must work, or the test proves nothing.
    cb_txid, cb_value = coinbases[0]
    low = spend(cb_txid, 0, key.p2pk_script(), key, key.p2pk_script(),
                cb_value - 100000, high_s=False)
    res, h = mine(rpc, [low], key)
    check("low-S spend accepted (control)", res is None,
          "h=%d %s" % (h, res or "accepted"))

    # The real test.
    cb_txid, cb_value = coinbases[1]
    high = spend(cb_txid, 0, key.p2pk_script(), key, key.p2pk_script(),
                 cb_value - 100000, high_s=True)
    res, h = mine(rpc, [high], key)
    check("HIGH-S spend accepted", res is None,
          "h=%d %s" % (h, "accepted" if res is None
                          else "REJECTED (%s) -- normalize() is missing" % res))

    print("\n%d/%d passed" % (sum(results), len(results)))
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
