#!/usr/bin/env python3
"""On-chain CLTV tests for (BOB): does ConnectBlock actually gate on height?

The unit tests drive EvalScript directly. This drives the whole node -- real
blocks, real signatures, real validation -- because the bug class that matters
here is a wiring error between the interpreter and ConnectBlock, which a unit
test cannot see.

Three properties, in the only order that can demonstrate them:

  1. BEFORE activation, a spend that VIOLATES its lock is still accepted. This
     is the soft-fork property. If it fails, activating would split the chain
     rather than tighten it.
  2. AFTER activation, that same violation is rejected.
  3. AFTER activation, a spend that SATISFIES its lock is accepted, so the rule
     tightened rather than simply breaking the opcode.

Two constraints shape the fixtures, and both are easy to get wrong:

  * IsFinalTx rejects a non-final transaction from a block outright. So every
    spend needs nLockTime BELOW the current height, or the block is refused for
    a reason that has nothing to do with CLTV.
  * Therefore a *satisfying* spend needs a lock that is small enough to have
    passed. A far-future lock can only be tested by violating it.

So: two outputs locked far ahead supply the violation cases, and one locked just
ahead supplies the satisfying case.

HARDFORK_CLTV_TESTNET is 150, above COINBASE_MATURITY (100), which is what makes
step 1 reachable: nothing is spendable before height 101.

Usage: ./cltv-chain-tests.py <datadir>/dobbscoin.conf
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cltvlib import Key, Rpc, cltv_script, mine_block, spend, txid

ACTIVATION = 150
# LWMA-3 activates at block 100 on the test networks and retargets every block,
# so mining with ~0 s solvetimes ramps difficulty until a pure Python miner
# cannot keep up. Advancing the node's clock with setmocktime by one target
# interval per block keeps solvetimes on target and difficulty flat.
SPACING    = 600     # nTargetSpacing_V3
FAR  = 900000        # a lock no reachable height satisfies
NEAR = 120           # a lock already passed by the time we test it
FEE  = 100000

results = []
coinbases = []       # (txid, value) of blocks we mined, in order
clock = [0]          # block timestamp, advanced one SPACING per block


def check(name, ok, detail=""):
    print("  [%s] %-44s %s" % ("PASS" if ok else "FAIL", name, detail))
    results.append(ok)


def mine(rpc, txs, key):
    clock[0] += SPACING
    rpc("setmocktime", clock[0])
    res, cb_txid, height, cb_value = mine_block(rpc, txs, key)
    if res is None:
        coinbases.append((cb_txid, cb_value))
    return res, height


def fill_to(rpc, key, height):
    while rpc("getblockcount") < height:
        res, _ = mine(rpc, [], key)
        if res is not None:
            raise RuntimeError("filler block rejected: %s" % res)


def main():
    rpc = Rpc(sys.argv[1])
    key = Key()
    # Start ~200 blocks' worth in the past so every timestamp is historical.
    clock[0] = int(time.time())
    rpc("setmocktime", clock[0])

    print("(BOB) CLTV on-chain tests -- activation at height %d" % ACTIVATION)
    fill_to(rpc, key, 101)
    print("  tip %d: coinbases spendable, CLTV not yet active" % rpc("getblockcount"))

    # Create the CLTV-encumbered outputs while the rule is still dormant --
    # exactly the pre-fork UTXO set a real activation inherits.
    locks = [FAR, FAR, NEAR]
    funded = []
    for i, lock in enumerate(locks):
        cb_txid, cb_value = coinbases[i]
        tx = spend(cb_txid, 0, key.p2pk_script(), key,
                   cltv_script(lock, key), cb_value - FEE)
        res, _ = mine(rpc, [tx], key)
        if res is not None:
            raise RuntimeError("funding block %d rejected: %s" % (i, res))
        funded.append((txid(tx), cb_value - FEE, lock))
    print("  3 CLTV outputs created (locks: %s)" % locks)

    def spend_of(idx, nlocktime):
        t, value, lock = funded[idx]
        return spend(t, 0, cltv_script(lock, key), key,
                     key.p2pk_script(), value - FEE,
                     nlocktime=nlocktime, nsequence=0)

    # 1. pre-activation: violating spend must be ACCEPTED (soft-fork property)
    res, h = mine(rpc, [spend_of(0, 50)], key)
    check("pre-activation: violating spend accepted", res is None,
          "h=%d %s" % (h, res or "accepted"))

    # 2. post-activation: the same violation must be REJECTED
    print("  mining to activation...")
    fill_to(rpc, key, ACTIVATION - 1)
    res, h = mine(rpc, [spend_of(1, 50)], key)
    check("post-activation: violating spend rejected", res is not None,
          "h=%d %s" % (h, res or "ACCEPTED -- height gate did not fire"))

    # 3. post-activation: a satisfying spend must be ACCEPTED
    res, h = mine(rpc, [spend_of(2, NEAR)], key)
    check("post-activation: satisfying spend accepted", res is None,
          "h=%d %s" % (h, res or "accepted"))

    print("\n%d/%d passed" % (sum(results), len(results)))
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
