# CLTV on-chain tests

`src/test/cltv_tests.cpp` drives `EvalScript` directly. These build **real blocks
on a regtest chain**, which is the only way to test the wiring between the script
interpreter and `ConnectBlock` — in particular that the activation height gate
actually selects the flag. A unit test cannot see a wiring error.

```bash
make -j$(nproc) -C src dobbscoind dobbscoin-cli
./qa/cltv/run-cltv-chain-tests.sh
```

Three properties, in the only order that can demonstrate them:

| | |
|---|---|
| **before** activation | a spend that **violates** its lock is still **accepted** |
| **after** activation | that same violation is **rejected** |
| **after** activation | a spend that **satisfies** its lock is **accepted** |

The first is the soft-fork property. If it failed, activating would split the
chain rather than tighten it — so it is the single most important line here.

## Why the fixtures look the way they do

Five things constrain this harness. Each one was found by writing it, and each
one silently produces a test that passes while proving nothing.

**`HARDFORK_CLTV_TESTNET` must exceed `COINBASE_MATURITY`.** It is 150 against a
maturity of 100. Nothing is spendable before height 101, so with activation at
20 there would be no height at which a *pre-activation spend* could exist, and
the soft-fork property would be untestable on chain. The constant is 150 for
this reason.

**`IsFinalTx` rejects a non-final transaction from a block outright.** Every
spend needs `nLockTime` below the current height or the block is refused for a
reason that has nothing to do with CLTV. Consequently a *satisfying* spend needs
a lock that has already passed — a far-future lock can only be tested by
violating it. Hence two outputs locked far ahead and one locked just ahead.

**LWMA-3 activates at block 100 on the test networks and retargets every block.**
Mining with ~0 s solvetimes ramps difficulty until a pure Python miner cannot
keep up; the chain becomes unmineable somewhere past height 130. The harness
advances the node's clock with `setmocktime` by one target interval per block,
which keeps solvetimes on target and difficulty flat.

**Let the node compute `nBits`.** Stamping an `nTime` that differs from the
template's `curtime` makes the node recompute a different target and refuse the
block as `bad-diffbits`. Because `setmocktime` moves the clock first, the
template's `curtime` and `bits` agree with what the node expects back — so use
both verbatim rather than deriving either. There are five retarget functions in
`pow.cpp` (`V1`–`V4` plus `LWMA3`); do not try to work out which one applies.

**OpenSSL 3 dropped `ripemd160` from its default provider**, so `hashlib` cannot
compute hash160 and P2PKH is unavailable. The harness uses pay-to-pubkey, which
needs no hash. Regtest does not require standard scripts.

## Dependencies

`python3-ecdsa`. The wallet is compiled out of the standard build, so there is no
`setgenerate` and no funding source: the suite builds keys, coinbases, blocks and
signatures by hand. Coinbase scriptSigs carry the BIP34 height, and blocks past
the AuxPoW fork at height 10 declare `AUXPOW_CHAIN_ID` in `nVersion`.

Signatures are strict-DER and low-S — BIP66 is enforced on this chain, so a
sloppy encoding would be rejected for the wrong reason.
