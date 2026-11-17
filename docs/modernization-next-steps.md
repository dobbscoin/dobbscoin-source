# Modernization Next Steps

This document is being updated during live runtime validation of the modernized `dobbscoind` daemon.

## Current validation status

- Runtime validation is using the real synchronized chain datadir at `/home/btcbob/.dobbscoin`.
- The daemon is running from the modernized source tree build.
- Monitoring is active and results are being written to `logs/runtime-validation-overnight.log`.
- No consensus or network-parameter changes are being introduced as part of this documentation pass.

## Warning classification rubric

- Build-breaking: prevents compilation or link on supported Ubuntu toolchains.
- Future-risk: compiles now, but likely to break on newer compilers, libraries, or stricter warning policies.
- Cosmetic: noisy but currently low-risk warnings that do not block build or runtime validation.

## Initial warning inventory

### Future-risk

- `src/leveldbwrapper.h`
  - Dynamic exception specifications (`throw(...)`) are deprecated and can become hard errors under newer language modes.
  - Safe minimal fix: replace dynamic exception specifications with `noexcept(false)` or remove them where exception propagation is already implicit.

- `src/leveldbwrapper.cpp`
  - Matches the same deprecated exception-specification pattern used by the wrapper declarations.
  - Safe minimal fix: keep declarations and definitions aligned while removing deprecated exception specs.

- `src/ecwrapper.cpp`
  - OpenSSL 3.x deprecation warnings remain around legacy EC and ECDSA APIs.
  - Safe minimal fix: migrate remaining calls to supported accessor/allocation APIs without altering key or signature semantics.

- `src/crypto/scrypt.cpp`
  - OpenSSL 3.x deprecation warnings remain in hashing primitives.
  - Safe minimal fix: switch deprecated digest helper usage to current OpenSSL 3-compatible EVP patterns or isolate legacy code behind compatibility wrappers.

### Cosmetic

- `src/script/script.h` and dependent translation units
  - Deprecated-copy warnings cascade from legacy copy constructors and assignment behavior.
  - Safe minimal fix: add explicit defaulted special members where the intent is plain value semantics.

- `src/key.h`, `src/main.h`, `src/wallet.h`, and dependent translation units
  - Similar deprecated-copy warnings on legacy value types.
  - Safe minimal fix: explicitly default copy operations or adjust constructors so the compiler no longer infers deprecated copy behavior.

- Multiple source files in the daemon build
  - Compiler hygiene warnings around implicit fallthrough, bounded `strncpy`, catch-by-value, and raw `memcpy` into small integer wrappers.
  - Safe minimal fix: add explicit fallthrough annotations/comments, tighten string-copy bounds handling, catch exceptions by const reference, and replace raw memory copies with clearer typed helpers where behavior is unchanged.

## Items already known not to be required for daemon-only success

- Berkeley DB / wallet modernization
  - Not required for the wallet-disabled daemon-only target.

- Qt GUI modernization
  - Not required for the GUI-disabled daemon-only target.

## Follow-up documentation plan

- Capture a fresh warning inventory from a clean single-job rebuild.
- Map warnings to exact files and representative call sites.
- Separate daemon-relevant work from wallet/GUI-only work.
- Propose smallest safe code changes that preserve consensus and network behavior.

## Incremental scan from clean rebuild

- Source: `logs/modernization-warning-scan.log` generated from `make clean && make -j1`.
- Result: build completed successfully; no build-breaking warnings were observed in the current Ubuntu-targeted daemon build.

### Build-breaking

- None observed in the clean rebuild. The current issues are warning-only.

### Future-risk

- `src/leveldbwrapper.h`
  - Why: Dynamic exception specifications are deprecated and become progressively less acceptable as the language mode moves forward.
  - Safe minimal fix: remove legacy `throw(leveldb_error)` specifications while preserving exception behavior.
- `src/leveldbwrapper.cpp`
  - Why: Definition-side match for the same deprecated exception-specification pattern.
  - Safe minimal fix: remove legacy `throw(leveldb_error)` specifications while preserving exception behavior.
- `src/ecwrapper.cpp`
  - Why: OpenSSL 3 deprecation warnings remain in legacy EC/ECDSA usage.
  - Safe minimal fix: continue migrating deprecated OpenSSL calls to OpenSSL 3-compatible allocation/accessor APIs without changing signature semantics.
- `src/crypto/scrypt.cpp`
  - Why: OpenSSL 3 deprecation warnings remain in hashing code paths.
  - Safe minimal fix: replace deprecated digest helper usage with current EVP-compatible helpers or a thin compatibility wrapper.

### Cosmetic

- `src/script/script.h`
  - Why: Deprecated-copy warnings fan out into transaction, miner, wallet, and test code because `CScript` has a user-provided copy constructor but no explicit copy assignment.
  - Safe minimal fix: explicitly default copy assignment alongside the existing copy constructor.
- `src/primitives/transaction.h`
  - Why: Value-semantic transaction/container types inherit the `CScript` copy-assignment warning noise.
  - Safe minimal fix: once `CScript` copy semantics are explicit, most of these transitive warnings should disappear without logic changes.
- `src/amount.h`
  - Why: Deprecated-copy warning on `CFeeRate` due to explicit copy constructor without explicit copy assignment.
  - Safe minimal fix: explicitly default copy assignment or default both copy special members.
- `src/key.h`
  - Why: Deprecated-copy warning on `CKey` for the same reason.
  - Safe minimal fix: explicitly default copy assignment if value copying is intended.
- `src/main.cpp`
  - Why: Additional class-memaccess warning from `memcpy` into `uint160` and transitive `CScript` warning sites.
  - Safe minimal fix: replace raw `memcpy` with typed assignment/copy for `uint160` construction.
- `src/tinyformat.h`
  - Why: Repeated implicit-fallthrough warnings in format parsing switch logic.
  - Safe minimal fix: add explicit fallthrough annotations or comments in the switch cases.

### Additional notes

- The clean rebuild also emitted `/usr/bin/ar: `u` modifier ignored since `D` is the default`; this is toolchain-noise from the archive invocation, not a Dobbscoin source issue.
- Many warning sites appear in tests and wallet-related translation units even though daemon-only deployment does not require them. They are still worth cataloging because they affect whole-tree maintainability.

## Incremental runtime observations

- Validation is attached to the live synchronized datadir and is monitoring the already-running daemon without restarting it.
- `[2026-03-25T02:28:32Z] snapshot pid=395216 height=1811647 headers=1811647 bestblock=f1ba328c74e1ac5c63651f7370bcec01720680f68b1bfbebb13617ef43f710a1 peers=4 cpu_pct=38.6 mem_pct=9.8 rss_kb=595728 read_bytes=319569920 write_bytes=31731712`
- `[2026-03-25T02:28:32Z] debug: 2026-03-25 02:14:57 ProcessMessage(version, 102 bytes) FAILED peer=21`
- `[2026-03-25T02:28:32Z] debug: 2026-03-25 02:15:54 ProcessMessage(version, 102 bytes) FAILED peer=22`
- `[2026-03-25T02:28:32Z] debug: 2026-03-25 02:17:55 ProcessMessage(version, 102 bytes) FAILED peer=24`
- `[2026-03-25T02:28:32Z] debug: 2026-03-25 02:18:37 ProcessMessage(version, 102 bytes) FAILED peer=25`
- `[2026-03-25T02:28:32Z] debug: 2026-03-25 02:20:48 ProcessMessage(version, 102 bytes) FAILED peer=26`
- `[2026-03-25T02:28:32Z] debug: 2026-03-25 02:21:59 ProcessMessage(version, 102 bytes) FAILED peer=27`
- `[2026-03-25T02:28:32Z] debug: 2026-03-25 02:24:01 ProcessMessage(version, 102 bytes) FAILED peer=28`
- `[2026-03-25T02:28:32Z] debug: 2026-03-25 02:24:18 ProcessMessage(version, 102 bytes) FAILED peer=29`
- `[2026-03-25T02:28:32Z] debug: 2026-03-25 02:27:05 ProcessMessage(version, 102 bytes) FAILED peer=30`
- `[2026-03-25T02:28:32Z] debug: 2026-03-25 02:27:29 ProcessMessage(version, 102 bytes) FAILED peer=31`

### Runtime follow-up candidates

- `ProcessMessage(version, 102 bytes) FAILED` entries in `~/.dobbscoin/debug.log` should be traced to confirm whether they reflect harmless peer handshake incompatibility or a protocol regression introduced by modernization.
- If these version-message failures continue while healthy peers remain connected and the tip advances, classify them as network-interop noise rather than a runtime blocker.


## Runtime monitor policy

- The detached watcher has been updated to run until manually stopped.
- If `dobbscoind` exits unexpectedly, the watcher will:
  - record the last error and recent `debug.log` lines
  - capture current process/resource state in the runtime log
  - restart the daemon once
  - continue five-minute monitoring snapshots after restart
- The watcher does not stop the daemon on its own unless it was the process that started the daemon during an unexpected-stop recovery path.
