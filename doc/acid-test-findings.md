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
- testnet/regtest auxpow chain-id left at Doge's (only CMainParams was switched to 0x00B0).
- SegWit/CSV: disabled on (BOB) (Segwit/CSV heights = INT_MAX). The whole point of the rebase is to
  ENABLE them going forward via a future (BOB) fork height — a deliberate new activation, not history.
- The base's nPowTargetSpacing/timespan + BIP9 deployment windows are Doge's; inert for (BOB) mainnet
  (custom retarget dispatcher covers all heights; CSV/Segwit gated off), but review before a release.
