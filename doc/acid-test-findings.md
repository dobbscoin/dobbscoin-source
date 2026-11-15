# (BOB) → modern-Core rebase: consensus port + from-genesis acid test

Branch: `scratch/bob-genesis` (chaos `~/bob-rebase/dogecoin-121dev`), base **Dogecoin Core 0.21.2**
(1.21-dev, commit 56b3c4994 — the only modern lineage with scrypt + AuxPoW + SegWit + CSV native).
Source of truth for (BOB) behavior: `dobbscoin-source` v0.14 worktree `guru:~/claude/bob-v014-wt`
(live mainnet node `vps1:/var/lib/dobbscoind-btcbob`, read-only, for validation data).

## Method (per consensus delta)

1. Read the exact rule from the (BOB) source.
2. Re-implement in Python, validate against **real** (BOB) blocks pulled from the live node
   (faithful SetCompact/GetCompact, 256-bit wrapping, truncate-toward-zero where BN_div applied).
3. Transcribe to C++ `arith_uint256` (NO OpenSSL/bignum — the eviction is a hard goal).
4. Build; commit as MWOWM.
5. Whole-history acid test: offline `-reindex` of the real chain, must reach the live tip hash.

## What is ported + validated (all green against real history)

| Delta | Where | Notes |
|---|---|---|
| Genesis + network identity | chainparams.cpp | magic a0fb1783, port 19985, prefixes 0/5/128, bech32 "bob", genesis 6d2d7d52…5969 / merkle 21e66ff8…8ca1; `CreateBobGenesisBlock`; genesis PoW exemption in CheckProofOfWork (HASHGENESISBLOCKPOW) |
| 5 retarget regimes | pow.cpp dispatcher | V1 (<13579) · KGW [13579,31597) · DigiShield V3 [31597,68425) T=600 · DigiShield V4 [68425,1888808) T=120 · LWMA-3 (≥1888808). Each validated vs real nBits. KGW ported WITHOUT OpenSSL (portable split mul/div). |
| Reward curve | dogecoin.cpp `GetDogecoinBlockSubsidy` (consensus path, validation.cpp:2234) | base 50 → 10@68425/≥69791 → halving-of-10 while ≤951752 → hard 1.5 from 951753 → 0.5→1.5 failsafe. Validated every segment. **It is a CAP** (coinbase check is `>`); real block 951753 pays 0.625 ≤ cap 1.5 (the hard-set deploy block, a legal underpayment). |
| BIP34/66/65 + AuxPoW heights | chainparams.cpp CMainParams | BIP34Height=1 (genesis is the only v1 block; block-1 coinbase starts 0x51 = CScript()<<1 = compliant), BIP34Hash=block 1. BIP66Height=68425 (v2→v3/DERSIG, exact). BIP65=2000000 (CLTV). nAuxpowChainId=0x00B0, nAuxPowHeight=2000000. Derived by scanning real block versions, not Doge defaults. |
| **Emergency-difficulty valve** | pow.cpp `BobIsEmergencyDifficultyBlock` + validation.cpp ContextualCheckBlockHeader | SEE BELOW — found by the acid test. |

## The acid-test bug: emergency-difficulty valve (THE key trap for core work)

The first from-genesis reindex rejected **real block 1888904** with `bad-diffbits`. Not a port
error in the formula — my LWMA-3 computes 1888905 correctly (it reads 1888904's real bits as
input and matches). Block 1888904 is a **legitimate minimum-difficulty block** (nBits=1e0fffff)
following a **~6.1h stall** (solvetime 21934 s). The retarget algorithm alone would never produce
min-difficulty there (it computed 1c3a2580 = normal), so there HAD to be a rule above it.

(BOB) has a two-sided emergency-difficulty valve (dobbscoin-source `pow.cpp`/`main.cpp`):
- **Validation** (`IsEmergencyDifficultyBlock`): a block is accepted at min difficulty iff
  (a) `pindexPrev->nHeight+1 >= HARDFORK_EMERGENCY_DIFF_MAIN (1888888)`, AND
  (b) `block.nTime - pindexPrev->nTime > EMERGENCY_DIFFICULTY_GAP (6h = 21600 s)` (strict `>`), AND
  (c) `block.nBits == ProofOfWorkLimit().GetCompact()` (= 1e0fffff).
- This is a **VALIDATION EXEMPTION**, NOT a branch inside GetNextWorkRequired. A stalled block may
  instead carry the retarget algorithm's stuck target and still pass the ordinary nBits check.
  Both are valid. (Mining side `GetNextWorkRequiredForMining` PREFERS min-diff after a stall, but
  that is miner policy only; old nodes accept via the v0.13.0 exemption → not a fork.)

Port (commit on branch): `BobIsEmergencyDifficultyBlock(block, pindexPrev, params)` in pow.cpp
(testnet fork height 20 via `fPowAllowMinDifficultyBlocks`), declared in pow.h, wired into the
bad-diffbits check at validation.cpp ContextualCheckBlockHeader:
`if (nBits != GetNextWorkRequired(...) && !BobIsEmergencyDifficultyBlock(...)) -> bad-diffbits`.

**WHY IT HID:** earlier LWMA-3 spot-checks (tip heights 1948061–1948100, bootstrap boundary
1888867–69) all passed — none sat after a >6h stall, so the valve never fired. 1888904 is the
first >6h-gap block past the 1888888 fork. LESSON for core work: any difficulty port must be
validated against **stall events**, not just steady-state blocks. Expect more emergency blocks at
every >6h gap past 1888888.

## Acid-test bug #2: coinbase maturity (another inherited-Doge-default trap)

With the diffbits valve fixed the reindex ran far past 1888904's header, then the **block-connection**
phase rejected a real tx at **height 145116**: `bad-txns-premature-spend-of-coinbase, tried to spend
coinbase at depth 115`. Different class — a ConnectBlock/CheckTxInputs rule, not difficulty.

- (BOB) uses a **flat `COINBASE_MATURITY = 100`** (main.h:81, Bitcoin-0.10 style).
- The Doge base uses `COINBASE_MATURITY = 60*4 = 240` plus a Doge-only early-chain rule:
  `COINBASE_MATURITY_OLD = 30` for coins below `COINBASE_MATURITY_SWITCH = 145000`, gated by
  `consensus.fShortEarlyCoinbase` (true in Doge CMainParams).
- The spent coinbase was at height 145001 (≥ the 145000 switch) → Doge demanded 240 > 115 →
  premature. (BOB) demanded 100 ≤ 115 → valid.

Fix: `COINBASE_MATURITY = 100` in consensus.h, and CMainParams `fShortEarlyCoinbase = false`
(flat 100 at every height). LESSON: audit EVERY Doge-specific consensus default against (BOB) —
they differ silently and only surface at the first historical block that exercises them. Known
Doge-specific knobs to check: fShortEarlyCoinbase (done), BIP16/P2SH activation, block size/weight
+ sigop limits, nPowTargetSpacing/timespan, BIP9 deployment windows, any `fSimplifiedRewards` path.

## Acid-test harness settings (chainparams) — REVERT/REPLACE for a production build

To validate the real chain offline these were changed and are **NOT production values**:
- `checkpointData` → (BOB) **genesis only** (so every later block is validated by pure
  consensus/PoW, no checkpoint shortcut). A real build should carry (BOB)'s real checkpoints.
- `nMinimumChainWork` → 0 (Doge's value is far above (BOB)'s low-difficulty chainwork).
- `defaultAssumeValid` → 0 (full script validation from genesis; Doge's hash isn't on (BOB)'s chain).
- `chainTxData` still Doge's (cosmetic; update for a release).

## Still untestable here / open for core work

- **AuxPoW + CLTV**: both activate at block **2,000,000** (HARDFORK_*_MAIN) — not yet reached on the
  live chain (tip ~1.95M), so this sync validates everything EXCEPT those two future rules. No real
  blocks exist to test them against yet. `fStrictChainId`, AuxPoW parent-chain checks, and the CLTV
  soft-fork path are ported-by-inheritance (Doge) + chain-id/height set, but UNVALIDATED against (BOB).
- testnet auxpow chain-id still Doge-default; regtest switched to 0x00B0 (see 2M-fork harness below).
- SegWit/CSV: disabled on (BOB) (Segwit/CSV heights = INT_MAX). The whole point of the rebase is to
  ENABLE them going forward via a future (BOB) fork height — a deliberate new activation, not history.
- The base's nPowTargetSpacing/timespan + BIP9 deployment windows are Doge's; inert for (BOB) mainnet
  (custom retarget dispatcher covers all heights; CSV/Segwit gated off), but review before a release.

## 2M-fork harness, AuxPoW half (2026-11-15)

Reading the base's AuxPoW path against the (BOB) 0.14 daemon (the code the network runs at the
fork) turned up five consensus differences. Every one stays hidden until block 2,000,000, so the
acid test could not catch them. All five are fixed here, and `test/functional/feature_auxpow_bob.py`
exercises each one.

1. **AuxPoW was never checked.** Dogecoin 1.21-dev deserializes the auxpow trailer but
   `CheckBlockHeader` and `ReadBlockOrHeader` only test the header's own scrypt hash. Nothing called
   `CAuxPow::check()` or tested the parent's PoW. As a result, a merge-mined block (whose own hash
   misses the target) was rejected, and the commitment chain was never verified.
   Fix: `BobCheckAuxPowProofOfWork` (pow.cpp), a port of 0.14 `CheckAuxPowProofOfWork`, used at both
   call sites.
2. **Chain-ID schedule missing.** 0.14 requires chain ID 0 before the fork and exactly 0x00B0 at and
   after it, for solo and merge-mined blocks alike. The base only had Doge's "v1, or v2 with chain 0,
   is legacy" test. Without the fix, a chain-ID-0 v3/v4 block after 2M was accepted, and so was a
   nonzero chain ID before 2M. Fix: the 0.14 rules in ContextualCheckBlockHeader
   (`premature-auxpow`, `premature-chainid`, `bad-chainid`).
3. **`nIndex` dropped.** The base read the parent-coinbase merkle index and discarded it. 0.14
   rejects any value other than 0 ("AuxPow is not a generate"). Fix: keep it in `CAuxPow` and check
   it.
4. **Chain split at 2M: base version 4 required at BIP65Height.** The Doge rule `GetBaseVersion() < 4
   && nHeight >= BIP65Height` would reject every block the 0.14 fleet mines after 2M, because 0.14's
   `CURRENT_VERSION` is 3 and its CLTV is height-gated with no version rule. Fix: drop the v4 rule
   for (BOB) and keep base version >= 3 from BIP66Height (0.14 enforces that by supermajority).
5. **Miner stamped chain ID before the fork.** CreateNewBlock always set 0x00B0, so a pre-2M block
   it mined would fail 0.14's `premature-chainid` check. Fix: chain ID only from nAuxPowHeight on.

Regtest now uses chain ID 0x00B0 and takes `-auxpowheight=<n>` (default 0, i.e. always on).
The test framework's `CHAIN_ID` is 0xB0.

`feature_auxpow_bob.py` (fork at 1400, past regtest BIP65Height 1351) asserts:
base-3 blocks accepted past BIP65Height; before the fork, chain ID 0x00B0 is rejected
(premature-chainid) and a merge-mined block is rejected (premature-auxpow); at the fork, chain ID 0
and Doge's 0x0062 are rejected (bad-chainid) and base 2 is rejected (bad-version). Malformed
auxpow is rejected in each of these cases: parent misses target while our own header would pass;
parent carries our chain ID; nIndex 1; commitment to another hash; magic twice; wrong chain index.
A merge-mined block whose OWN hash misses the target is accepted on its parent's PoW. A solo
chain-ID block is accepted. After a restart, blocks read back from disk and pass `verifychain 4`.

Still open for the fork:
- **Differential vs the 0.14 daemon.** The tests above encode 0.14's rules as read from source. The
  two regtests have different genesis blocks, so the same blocks cannot simply be fed to both. Next
  step: either a shared regtest genesis, or 0.14's `qa/auxpow-smoke/` vectors replayed here.
- **No merge-mining RPC.** The base has no `createauxblock`/`submitauxblock`/`getauxblock`. Pools
  need one at 2M (0.14 has `submitauxblock`). Not consensus, but required before a release.
- Unit tests `subsidy_limit_test`, `miner_tests/CreateNewBlock_validity` and
  `validation_block_tests` fail identically before and after this change. They are Doge-mainnet
  expectations left from the earlier ports and need (BOB) values.

## Differential vs the (BOB) 0.14.1 daemon (2026-11-15)

`contrib/bob-differential/diff_vs_014.py` runs the 0.14.1 release (`1a300eb0`, the CI build) and
this rebase side by side on the same regtest. It builds every probe block once, submits it to both
daemons, and treats the 0.14 verdict as the reference. Blocks from each daemon's own miner go to the
other daemon: 0.14 `setgenerate` and `createauxblock`+`submitauxblock`, and the rebase's
`generatetodescriptor`. At the end both chains must match block for block.

To make that possible, regtest is now (BOB) 0.14's regtest: genesis `63c57ac1…a07f` (the mainnet
coinbase, nTime 1296688603, nonce 2), AuxPoW at 10, CLTV at 150 (`-bip65height` overrides), and
SegWit/CSV off as on mainnet. The CLTV test now runs at 150.

Result: **178 checks, 0 divergences, chains identical (157 blocks).** Covered:
- pre-fork base versions 1-4, plus chain-ID and merge-mined blocks before the fork
- 13 malformed-auxpow cases at the fork
- 5 valid merge-mining shapes: own hash missing the target, the no-header legacy commitment, a
  chain branch of 2, a parent tree with a sibling, and own hash also valid
- solo blocks with chain ID at base 1, 2 and 3
- 0.14 RPC merge-mined blocks
- 0.14 miner blocks after both forks, at base 3 (the chain-split case)
- the rebase miner's blocks before and after the fork
- 6 CLTV cases around height 150

The harness catches real bugs: a mutant with the `nIndex` check removed and the base-v4 rule
restored produced 3 divergences plus a FATAL on the first 0.14-mined block after CLTV.

Run: `python3 -I contrib/bob-differential/diff_vs_014.py src <ref-0.14-dir> <workdir>`. The ref dir
holds `dobbscoind`, `dobbscoin-cli` and `lib/` (only `libminiupnpc.so.17` is needed on Debian
trixie).
