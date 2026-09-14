# AuxPoW regtest tests

Self-contained tests for the AuxPoW merge-mining path, runnable against a
freshly built `src/dobbscoind`. Two suites:

* **`regtest-smoke.sh`** — the createauxblock / submitauxblock **RPC contract**.
* **`run-parent-pow-tests.sh`** — the **consensus** path, including a real
  scrypt proof of work on a synthetic parent chain.

Activation on regtest and testnet is `HARDFORK_AUXPOW_TESTNET` = **block 10**
(`src/pow.h`); mainnet is `HARDFORK_AUXPOW_MAIN` = block 2,000,000. Because the
test networks activate at 10, the entire fork is observable on a chain of a
dozen blocks using the same code path mainnet will use.

## Suite 1 — RPC contract

`regtest-smoke.sh`:

| Test | What it confirms |
|---|---|
| 1 | createauxblock refuses before activation |
| 2 | Post-activation response has 7 documented fields, chainid == 176 |
| 3 | submitauxblock rejects unknown block hash |
| 4 | submitauxblock rejects malformed auxpow hex |
| 5 | Repeat createauxblock returns cached template (or fresh if mempool drifted) |
| 6 | Advancing the chain tip invalidates the cache |

It provisions a temp datadir, starts the daemon with `-rpcuser=auxpow
-rpcpassword=test`, generates blocks past activation, runs the tests, and
cleans up on exit.

```bash
make -j$(nproc) -C src dobbscoind dobbscoin-cli
./qa/auxpow-smoke/regtest-smoke.sh
```

## Suite 2 — parent proof of work

`run-parent-pow-tests.sh` (driving `parent-pow-tests.py` / `auxpowlib.py`)
covers what suite 1 leaves out: a `CAuxPow` that actually satisfies the
parent-PoW check.

This was originally assumed to need real MiningCore. It does not. The regtest
target is loose enough that a synthetic parent block solves in a handful of
scrypt attempts in pure Python, so the full consensus path is a test rather
than a runbook.

```bash
./qa/auxpow-smoke/run-parent-pow-tests.sh
```

One valid merge-mined block must be **accepted**; eight proofs, each mutating
exactly one field of an otherwise valid one, must all be **rejected**:

| Mutation | Check it must trip |
|---|---|
| parent block claims our own chain ID | `AuxPow parent has our chain ID` |
| no commitment in the parent coinbase | `AuxPow missing chain merkle root` |
| declared tree size ≠ 1 << branch length | `AuxPow merkle branch size does not match` |
| two `fabe6d6d` headers in one coinbase | `Multiple merged mining headers` |
| parent PoW does not meet target | `proof of work failed` |
| coinbase not at index 0 | `AuxPow is not a generate` |
| commitment truncated, nonce absent | `AuxPow missing chain merkle tree size and nonce` |
| commits to a different block's hash | `AuxPow missing chain merkle root` |

Each rejection is checked against the validator error it was **supposed** to
trigger, read out of `debug.log`, not merely against the fact that the RPC
said no. A test that passes for the wrong reason is not a test.

> ⚠ Two traps, both paid for:
>
> * The negative PoW case must grind for a header that **provably fails**.
>   Handing back nonce 0 unchecked is not good enough — the regtest target is
>   so loose that a random header meets it roughly half the time, so the case
>   passes only by luck and fails intermittently.
> * `CAuxPow::check()` byte-reverses the computed root before searching the
>   parent coinbase for it. The hash hex returned by `createauxblock` is
>   already in that reversed display order, so the bytes to embed are plain
>   `bytes.fromhex(hash)` — reversing again silently produces a proof that is
>   rejected as "missing chain merkle root".

## What these do and do not establish

They establish that the daemon accepts well-formed merge-mined blocks and
refuses malformed ones, and that the activation boundary behaves: blocks below
the fork height carry chain ID 0, blocks at or above it carry 176 with the
base version still 3, so BIP66 enforcement is unaffected by the fork.

They say nothing about **payout policy** — who receives the coinbase when a
foreign pool finds our blocks. That is a governance question, not a code one.

Note also that merge mining is *optional* after activation: the post-fork rule
requires the chain ID, not the AuxPoW bit, so natively mined scrypt blocks
remain valid. Activation does not force anyone to merge-mine.

## Real-MiningCore runbook

Suite 2 replaces MiningCore for correctness testing, but a live deployment is
still worth doing before mainnet activation, because it exercises stratum, job
distribution and a real miner:

1. Run `dobbscoind -regtest -rpcuser=… -rpcpassword=…` with `-rpcport` set to
   whatever your MiningCore config expects.
2. Generate enough regtest blocks to cross activation (block 10).
3. In MiningCore's coin configuration, set the merge-mining child pool to use:
   - `createAuxBlock` RPC: `createauxblock`
   - `submitAuxBlock` RPC: `submitauxblock`
   - `chainId`: `176` (0x00B0)
   - Coin algorithm: `scrypt`
4. Start MiningCore and point an actual scrypt miner at its stratum endpoint.
5. Observe submitauxblock landing — `getblock` on the new tips should show the
   VERSION_AUXPOW bit set, i.e. versions of the form `0x00b001xx`.
