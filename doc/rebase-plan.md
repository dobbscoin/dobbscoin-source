# (BOB) Core → modern-Core rebase plan (SegWit enablement)

Status: PLAN / scoping. Opened 2026-11-14. Author: MWOWM.
Driver: the native XMR↔(BOB) atomic-swap desk needs **SegWit** (txid-malleability fix the
pre-signed-tx swap depends on) + **CSV** (script timelocks). (BOB) has neither, and they are not
addable as a parameter flip — the codebase is an old base with none of the prerequisites.

## 1. What (BOB) is today (verified against this source + the live mainnet node)

- **Base:** an old ~Bitcoin-Core-0.10 tree, self-numbered "0.14.1". No BIP9 version-bits
  (`versionbits.cpp` absent), no `bech32.cpp`, **no SegWit anything** (no `CTxWitness`,
  `vtxinwit`, `IsWitnessProgram`, witness tx serialization). CSV opcode exists in
  `interpreter.h` but is **not consensus-enforced**.
- **PoW:** scrypt (`CBlock::GetPoWHash`, `pow.cpp` `CheckProofOfWork`).
- **Difficulty retarget — a versioned history (the hard part of any rebase):**
  `pow.cpp` switches `DiffMode` by height: V1 (Bitcoin-style) → V2 **KimotoGravityWell** →
  V3/V4 **DigiShield** → V5 **LWMA-3** (active since `HARDFORK_LWMA3_MAIN = 1,888,808`).
- **Pending forks at block 2,000,000 (~late Jan 2027):** `HARDFORK_CLTV_MAIN = 2000000`
  **and** `HARDFORK_AUXPOW_MAIN = 2000000` — CLTV + **AuxPoW (merged mining)** activate together.
  **SegWit is NOT in this bundle.** Tip (1.95M) is block version 3, no AuxPoW bit yet.
- **Money:** `COIN = 1e8` (**8 decimals**), `MAX_MONEY = 1e10 * COIN` (10B cap).
- **Genesis:** `nTime 1389086657` (2014-01), `nBits 0x1e0ffff0`, `nNonce 2084524493`, v1.
- **Network identity:** magic `a0 fb 17 83`; default port `19985`; base58 prefixes
  PUBKEY=0, SCRIPT=5, SECRET=128; ext keys = Bitcoin mainnet (`0488B21E`/`0488ADE4`).
- **Seeds:** `seed.dobbscoin.info`, `seed1..10.dobbscoin.info`, + themed
  `{jhvh1,dobbs,eris,connie,nheeghee}.23skidoo.info`.
- **Checkpoints:** 15. **Reward curve:** `nSubsidyHalvingInterval = 210000` present; effective
  reward TBD-verify (memory: ~1.5 BOB). **Effective block spacing** evolved across regimes
  (V1 `nTargetSpacing = 600s`; avg since genesis ≈ 3 min) — read exact target per regime.

## 2. Base decision: **Dogecoin Core**

(BOB) becomes **scrypt + AuxPoW** at block 2M. The only actively-maintained modern Core that
ships scrypt PoW **and** AuxPoW **and** SegWit+CSV+Taproot is **Dogecoin Core** (itself a
Litecoin/Bitcoin-Core-0.21 lineage). Litecoin Core = scrypt+SegWit but **no AuxPoW**; Bitcoin
Core = neither. Dogecoin Core covers (BOB)'s PoW model and hands us SegWit for free, and it
matches where (BOB)'s own roadmap (AuxPoW at 2M) is already heading.
Target a recent stable Dogecoin Core release; confirm exact tag at stage 1.

## 3. Consensus deltas to carry onto the base (the spec)

Base provides (reuse): scrypt GetPoWHash, AuxPoW, SegWit, CSV, Taproot, version-bits, bech32,
modern wallet/RPC/p2p. We re-apply (BOB)'s identity + **full historical consensus**:

1. **Network identity:** magic `a0fb1783`, port 19985, prefixes 0/5/128 + BTC ext keys, genesis
   (the 2014 block + its hash assert), the DNS seed list, 15 checkpoints.
2. **Money:** 8-decimal COIN, 10B MAX_MONEY, the reward curve (verify exact).
3. **🔴 Historical difficulty, by height (consensus-critical):** reproduce V1 → KGW → DigiShield
   (V3/V4) → LWMA-3 exactly at their fork heights, so the rebased node validates all ~1.95M
   existing blocks byte-identically. This is the single hardest, highest-risk carry.
4. **AuxPoW activation height** = 2,000,000 — reconcile (BOB)'s activation with Dogecoin Core's
   AuxPoW (chain-id, block-version scheme). (BOB)'s lands at 2M; align the version/aux rules.
5. **CLTV** = height-gated at 2,000,000 (already in (BOB); base has CLTV, just match the height).

## 4. Stages (regtest → testnet → sync-audit → activation)

1. Build Dogecoin Core clean; pin the exact base tag.
2. Re-apply (BOB) network identity + money + genesis → a node that *is* (BOB) on a modern base
   (won't yet validate mainnet history).
3. Port the historical consensus (retarget schedule by height, scrypt params, reward, AuxPoW@2M,
   CLTV@2M). **Acid test:** a full sync from genesis reaches the *same tip hash* as the current
   `/EndTimes:0.14.0` node. Until that matches, nothing else matters.
4. SegWit + CSV: confirm the base's deployments, set a **future** activation height for (BOB).
5. Testnet soak; then a coordinated mainnet activation (pool, seeds, miners) — a planned fork,
   far out, btcbob's call. Precedent: the 2M CLTV/AuxPoW height-gate.

## 5. Risks

- 🚨 Consensus-critical: any divergence in historical validation = chain split. Stage 3 sync-audit
  against the live chain is the gate; no mainnet move without it.
- The 2M CLTV+AuxPoW fork lands ~Jan 2027 regardless — the rebased node must honor it identically.
- Months-long effort. This is chain infrastructure, not a feature branch.
- Downstream: SegWit+CSV unblock the native XMR↔(BOB) atomic-swap DEX (see the Haveno/DEX plan).

## Appendix: verified consensus constants (extracted from source 2026-11-14)

**Reward (`GetBlockValue`, main.cpp:1281, mainnet):**
- Base 50 COIN; `nSubsidy >>= nHeight/210000` (halving) applies only while `nHeight <= 951752`.
- Explicit 10-COIN steps at height 68425 and >=69791.
- **Height >= 951753: hardset 1.5 COIN forever** (+ failsafe to 1.5 by 10,500,000). Current tip (1.95M) reward = **1.5 BOB**. Confirms the "1.5 forever" memory.

**Block spacing / retarget by height (pow.cpp `GetNextWorkRequired` DiffMode switch, mainnet):**
| Height range | DiffMode | Algo | Target spacing |
|---|---|---|---|
| [0, 13579) | 1 | V1 (Bitcoin-style) | 600s (10 min) |
| [13579, 31597) | 2 | KimotoGravityWell | 600s |
| [31597, 68425) | 3 | DigiShield ("dobbshield") V3 | 600s |
| [68425, 1888808) | 4 | DigiShield V4 | **120s (2 min)** |
| [1888808, ∞) | 5 | **LWMA-3** (N=60, T=120s) + max-reorg | 120s |

So (BOB) is 10-min blocks until 68425, **2-min since**. All 5 regimes must be reproduced by height for a from-genesis sync to match.

**PoW:** scrypt **N=1024, r=1, p=1** (`scrypt_1024_1_1_256`) — identical to Litecoin/Dogecoin; Dogecoin Core provides it natively.

**AuxPoW:** `AUXPOW_CHAIN_ID = 0x00B0` (176, "B0"=BOB); Namecoin/Doge-style merged mining (`CAuxPow::check`); **activates at height 2,000,000** (same block as CLTV). Dogecoin Core's AuxPoW is the same construction — set chain-id 0x00B0 + activation height.

**Checkpoints (mainnet, 13):** 2016, 4032, 8064, 12096, 13422, 14496, 15809, 15882, 30800, 66138, 68900, 318371, **1848000** (`0xf4e2e967…3615`). (Height-0 rows in chainparams are testnet/regtest.)

**Fork heights (mainnet):** KGW 13579 · DigiShield 31597 · 2-min/V4 68425 · LWMA-3 1,888,808 (active) · CLTV 2,000,000 · AuxPoW 2,000,000.
