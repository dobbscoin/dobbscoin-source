# -assumevalid on-chain tests

```bash
make -j$(nproc) -C src dobbscoind dobbscoin-cli
./qa/assumevalid/run-assumevalid-tests.sh
```

`-assumevalid=<hash>` tells a node that the block at that hash, and everything
before it, was checked by somebody else, so it can connect those blocks without
verifying their signatures. It is a local sync-speed setting. It relaxes no
consensus rule: every other check still runs on every block, and a block that
fails one is still rejected.

## Why there are two nodes

The gate in `ConnectBlock` only opens when the assumevalid block is already in
`mapBlockIndex` and on the header chain being followed, at the moment an earlier
block is connected. A single node fed blocks with `submitblock` can never be in
that state: it learns about block 120 only after it has connected block 119. The
ordering exists during headers-first sync and nowhere else, so node A mines the
chain and node B syncs it over p2p. **B is what is under test.**

This is the property most worth remembering here: a one-node version of this
harness would pass every assertion by never skipping anything.

## What the seven cases cover

| | |
|---|---|
| `-assumevalid=<tip>` | every block skipped, the named block included |
| `-assumevalid=<block 110>` | skipped up to 110, verified above it |
| `-assumevalid=0` | nothing skipped, node still syncs |
| a hash in no index | nothing skipped |
| genesis | nothing skipped: it is an ancestor of the chain but of no block being connected |
| `deadbeef`, non-hex | refused at startup, not silently treated as zero |
| `dobbscoind -?` | documents the option and prints the mainnet default |

The observable is the `-debug=assumevalid` line in `ConnectBlock`. That line
proves the gate opened. What proves the gate controls real verification is the
sabotage run below.

## Proof by sabotage

The log line says the branch was taken. It does not say signatures went
unchecked. To close that, patch `CheckInputs` so that any script check that
actually runs aborts the block:

```c
        if (fScriptChecks) {
            if (GetBoolArg("-sabotagescriptchecks", false))
                return state.DoS(100, error("SABOTAGE: a script check ran for tx %s",
                                            tx.GetHash().ToString()),
                                 REJECT_INVALID, "sabotage-script-check");
```

Rebuild, then sync B twice against the same chain:

| run | result |
|---|---|
| `-assumevalid=<tip> -sabotagescriptchecks=1` | syncs the whole chain |
| `-assumevalid=0 -sabotagescriptchecks=1` | stops at the last block before a spend |

The control run is the half that matters. It shows the chain does contain
signatures that would be verified, so the first run syncing is not an artifact of
there being nothing to check. Revert the patch before committing.

## Five things that shape the fixtures

**`setgenerate` is unusable past height 10.** The AuxPoW fork trips at block 10
on regtest, and every block above it must declare `AUXPOW_CHAIN_ID` in
`nVersion`. The internal miner does not, so `setgenerate true 101` spins forever
on `CheckProofOfWork() : hash doesn't match nBits` with no other symptom. Blocks
are built by hand through `qa/cltv/cltvlib.py`, which already handles this.

**The chain has to contain real spends.** 101 blocks of coinbases exercise no
script at all: `ConnectBlock` skips coinbase inputs. Ten blocks each carrying one
P2PK spend of a matured coinbase are what make the script path reachable, and the
first assertion in the suite checks they are there. Without them the whole suite
passes while proving nothing.

**LWMA-3 retargets every block on regtest.** Mining with ~0 s solvetimes ramps
difficulty past what a Python miner can solve. The harness advances the node's
clock with `setmocktime` by one target interval per block. See `qa/cltv/README.md`,
which found this first.

**P2PK, not P2PKH.** OpenSSL 3 dropped ripemd160 from its default provider, so
`hashlib` cannot compute hash160.

**`dobbscoind -daemon` forks without closing the stdio it inherited.** A
`subprocess` pipe never sees EOF and the test hangs forever with the node running
happily beside it. Give the daemon a file. The same fork is why a bad
`-assumevalid` value still exits the parent with status 0: `AppInit2` runs in the
child, so the failure has to be read out of that file, or inferred from the node
never answering RPC.
