# Retarget port spec — (BOB) GetNextWorkRequired → Dogecoin Core 0.21.2

Source of truth: (BOB) `src/pow.cpp` (dobbscoin-source). Target: replace the 0.21.2
`GetNextWorkRequired(pindexLast, pblock, const Consensus::Params& params)` body (pow.cpp:14)
with (BOB)'s DiffMode-by-height dispatcher calling the five ported regimes. Source fully read
2026-11-14; this spec makes the coding mechanical and reviewable.

## Dispatcher (mainnet heights, by `pindexLast->nHeight + 1`)
- regtest: `params.fPowNoRetargeting` → return `pindexLast->nBits`.
- `< 13579`            → V1  (Bitcoin-style)
- `[13579, 31597)`     → V2  (KimotoGravityWell)
- `[31597, 68425)`     → V3  (DigiShield, 10-min)
- `[68425, 1888808)`   → V4  (DigiShield, 2-min)
- `>= 1888808`         → V5  (LWMA-3)      [HARDFORK_LWMA3_MAIN]
Testnet switch heights: 20 / 50 / 200 / HARDFORK_LWMA3_TESTNET(100); testnet uses the
`fPowAllowMinDifficultyBlocks` branches inside each regime.

## Per-regime translation (0.10 → 0.21 API)
**V1 / V3 / V4** (mechanical): (BOB) uses `uint256 bnNew; bnNew.SetCompact(nBits); bnNew *= nActualTimespan; bnNew /= timespan;`
→ 0.21: `arith_uint256 bnNew; bnNew.SetCompact(nBits); bnNew *= (uint32_t)nActualTimespan; bnNew /= (uint32_t)timespan;`
- `Params().ProofOfWorkLimit()` → `UintToArith256(params.powLimit)`; compare with `>` and clamp.
- `Params().AllowMinDifficultyBlocks()` → `params.fPowAllowMinDifficultyBlocks`.
- V1 needs Interval=2016 + TargetTimespan=14*24*60*60 + TargetSpacing=600 (add as (BOB) consts; Doge's params differ). V3/V4 hardcode their own timespan/interval already (10min/2min, interval=1).
- Width check: nActualTimespan bounded by timespan*4 — V1 ≤ 4.8M, V3/V4 tiny — all < 2^32, so `(uint32_t)` casts are safe.

**V2 / KGW** (carry the bignum wrapper): KGW's intermediate math overflows 256 bits, so (BOB) uses OpenSSL BN. Port = copy (BOB) pow.cpp's helper block verbatim into 0.21 pow.cpp:
`CAutoBN_CTX`, `BignumPtr`, `NewBignum`, `CopyBignum`, `BignumFromCompact`, `CompactFromBignum`,
`BignumFromUint64`, `BignumFromUint256`, `Uint256FromBignum`, `Add/Subtract/Multiply/DivideBignums`,
+ `#include <openssl/bn.h>` (Doge 0.21.2 still links libssl — confirm at build). KGW body ports unchanged;
`Params().ProofOfWorkLimit()` → `params.powLimit` fed through `BignumFromUint256`. Spacing=600,
PastBlocksMin/Max from the 0.0625d / 1.75d window.
⚠ This re-introduces OpenSSL into consensus for KGW only — acceptable (matches (BOB)); flag for review.

**V5 / LWMA-3**: (BOB) uses `target / (uint64_t)(N*k)` and `sumTarget * (uint64_t)weightedTime`.
Compute the scalars: N=60, T=120, k = N(N+1)T/2 = 219,600; N*k = 13,176,000; max weightedTime ≈ 6T·N(N+1)/2 = 1,317,600.
Both < 2^32 → use `arith_uint256::operator/=((uint32_t)(N*k))` and `operator*=((uint32_t)weightedTime)`.
Overflow sanity (from (BOB)'s own invariant comments): divide-before-add keeps each term ≤ ~2^212, final nextTarget ≤ ~2^240 — safe in 256. Keep the [-6T,6T] solvetime clamp and the k/3 floor exactly.

## (BOB) helpers to surface in the 0.21 build
Fork-height constants (HARDFORK_LWMA3_MAIN=1888808, _TESTNET=100; literal 13579/31597/68425) and the
V1 interval/timespan/spacing constants. Put in pow.cpp or a bob-consensus header.

## Validation (do NOT trust the port until this passes)
1. **Per-regime nBits check** (bounded, fast, pinpoints bugs): pull real (BOB) headers straddling each
   boundary from the live node — 13578/13579, 31596/31597, 68424/68425, 1888807/1888808 — plus random
   samples inside each regime. For each, run the ported `GetNextWorkRequired` on the real preceding chain
   and assert output == the real on-chain nBits. A mismatch localizes to one regime.
2. **Full from-genesis sync** vs the live chain: reach the live tip hash. This is the whole-history acid test.
Only after (2) passes clean does any mainnet activation height get discussed. regtest → per-regime check → sync.

## Risk
Consensus-critical: a wrong retarget splits the chain. KGW's BN semantics and LWMA-3's operand widths are
the subtle spots. This is review-before-merge work, not autopilot.
