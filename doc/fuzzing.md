Fuzzing the wallet file readers
==============================

v0.14.0 reads every user's old Berkeley DB `wallet.dat` without Berkeley DB
(`src/bdbro.cpp`) and converts it to SQLite (`src/db.cpp`). Those files come
from disk, so they may be truncated, damaged or deliberately malformed. The
harnesses in `src/test/fuzz/` feed random files to that code under
[libFuzzer](https://llvm.org/docs/LibFuzzer.html) with AddressSanitizer and
UndefinedBehaviorSanitizer, and check that it rejects or reads them without
crashing and without touching the user's file.

| Harness | What it feeds | What it checks |
|---|---|---|
| `bdbro_parse` | a Berkeley DB file to `BerkeleyRO::ReadAll` | only `std::runtime_error` comes out; success needs the btree magic and a full page; the file is never written; a second read gives the same records |
| `bdbro_salvage` | a damaged Berkeley DB file to `BerkeleyRO::Salvage` (`-salvagewallet`) | never throws; `false` only with no records; no key twice; on a file `ReadAll` accepts, exactly `ReadAll`'s records; the outer "main" pointer is never returned as a record; the file is never written |
| `wallet_migrate` | `wallet.dat` in an empty datadir to `CDBEnv::MigrateFromBerkeley` | not Berkeley DB: nothing touched. Failure: `wallet.dat` byte-identical, nothing else left, and `ReadAll` had rejected it. Success: only `wallet.dat` and `wallet.dat.bdb-<time>`, the backup byte-identical to the input, the new SQLite records exactly what `ReadAll` read |
| `wallet_sqlite` | an SQLite `wallet.dat` to `CDBEnv::Verify`, a `CDB` handle (version read, cursor walk, lookups) and `ReadSQLiteWalletFile` | no crash; Berkeley DB files refused; on a file that passes `Verify` (the node's startup check), the cursor always moves forward, every key it returns is found again, and the cursor and the file reader agree |
| `wallet_recover` | a damaged `wallet.dat` (either format) to `CWalletDB::Recover` (`-salvagewallet`) | failure: `wallet.dat` byte-identical, nothing else left. Success: only `wallet.dat` and `wallet.dat.<bdb\|salvage>-<time>`, the backup byte-identical, every recovered record a key record that the salvage read returned |
| `wallet_load` | wallet *records* to `CWallet::LoadWallet` (so `ReadKeyValue`), in the in-memory store | no crash, no exception, a known result code |

The harnesses run with the clock frozen (`SetMockTime`), log nothing, and work
in a private directory under `/dev/shm` (set `BOB_FUZZ_TMPDIR` to use another
place). They never look at a real datadir.

Build
-----

It needs clang, which is not the compiler of the normal build. Use a build
directory of its own; everything in it is built with sanitizers.

    sudo apt-get install clang llvm      # Debian 12: clang 14
    CC=clang CXX=clang++ cmake -B build-fuzz -DBUILD_FUZZ=ON \
        -DBUILD_DAEMON=OFF -DBUILD_CLI=OFF -DBUILD_TX=OFF \
        -DBUILD_CONSENSUS_LIB=OFF -DBUILD_TESTS=OFF \
        -DWITH_MINIUPNPC=OFF -DWITH_NATPMP=OFF
    cmake --build build-fuzz -j4

The harnesses land in `build-fuzz/src/test/fuzz/` (`bdbro_parse`,
`bdbro_salvage`, `wallet_migrate`, `wallet_sqlite`, `wallet_recover`,
`wallet_load`). With gcc, `BUILD_FUZZ=ON` prints a warning and builds nothing
extra. `BUILD_FUZZ` turns `REDUCE_EXPORTS` and `ENABLE_HARDENING` off in that
build directory: `--exclude-libs,ALL` hides ASan's `malloc` from libstdc++,
and every program then dies at startup with "attempting free on address which
was not malloc()-ed".

Seeds
-----

`src/test/fuzz/corpus/<harness>/` holds a few small starting files: the
Berkeley DB fixtures (`qa/rpc-tests/data/wallet-v0.13.8-bdb-*.dat`,
`src/test/data/bdb_fixture.raw`), SQLite wallets made from them, and their
records in `wallet_load`'s input format. They are written by

    build-fuzz/src/test/fuzz/fuzz_seedgen . src/test/fuzz/corpus

Leave that directory alone otherwise. The corpus a fuzzer grows goes
elsewhere (below) and is not committed. libFuzzer writes new inputs into the
*first* directory on its command line, so never name the seed directory first
(or alone) in a fuzzing run.

Run
---

`src/test/fuzz/run.sh` runs one harness with the settings used on guru:

    FUZZ_SECONDS=120 src/test/fuzz/run.sh bdbro_parse

What it runs, spelled out:

    build-fuzz/src/test/fuzz/bdbro_parse \
        -max_total_time=120 -rss_limit_mb=2048 -timeout=10 -max_len=32768 \
        -artifact_prefix=$HOME/bob-fuzz-out/bdbro_parse/crashes/ \
        $HOME/bob-fuzz-out/bdbro_parse/corpus src/test/fuzz/corpus/bdbro_parse

libFuzzer adds new interesting inputs to the first directory and only reads the
second. `BOB_FUZZ_OUT` changes `$HOME/bob-fuzz-out`. Each harness uses one CPU
core. Leave it running unattended, at low priority, with a cap on CPU and memory:

    export XDG_RUNTIME_DIR=/run/user/$(id -u)   # if systemd-run cannot find the user bus
    systemd-run --user --unit=bob-fuzz-bdbro_parse \
        -p CPUQuota=100% -p MemoryMax=4G \
        --setenv=BOB_FUZZ_OUT=$HOME/bob-fuzz-out \
        nice -n 19 $PWD/src/test/fuzz/run.sh bdbro_parse

    systemctl --user status bob-fuzz-bdbro_parse      # running? how long?
    journalctl --user -u bob-fuzz-bdbro_parse -n 20   # libFuzzer's last lines
    systemctl --user stop bob-fuzz-bdbro_parse        # stop it

A status line looks like
`#1048576 pulse cov: 412 ft: 1650 corp: 180/620Kb exec/s: 900 rss: 70Mb`:
`cov` is code edges reached, `corp` the corpus size, `exec/s` the speed. When
`cov` stops rising for hours the harness has found what it can.

Crashes
-------

A crash, sanitizer report, invariant failure (`==== FUZZ INVARIANT FAILED ====`),
timeout or out-of-memory stops the run and saves the input as
`crashes/crash-<sha1>` (or `leak-`, `timeout-`, `oom-`). The log (journal or
terminal) holds the stack trace.

Reproduce it by passing the file instead of a directory, with the same
sanitizer settings `run.sh` uses (UBSan only stops the program when
`halt_on_error=1` is set):

    export UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1:suppressions=$PWD/src/test/fuzz/ubsan.supp
    export ASAN_OPTIONS=detect_leaks=1     # 0 for wallet_load and wallet_recover
    build-fuzz/src/test/fuzz/bdbro_parse ~/bob-fuzz-out/bdbro_parse/crashes/crash-<sha1>

Shrink it to the smallest input that still fails:

    build-fuzz/src/test/fuzz/bdbro_parse -minimize_crash=1 -runs=10000 \
        -exact_artifact_path=/tmp/min.dat ~/bob-fuzz-out/bdbro_parse/crashes/crash-<sha1>

Look inside it (`xxd /tmp/min.dat | head`), and for a Berkeley DB file compare
with the page layouts in `src/bdbro.cpp`. For `wallet_load`, an input is
key, value, key, value... each ending in a backslash followed by any other
byte (`\\` is a literal backslash).

Debug it with gdb (`gdb --args build-fuzz/src/test/fuzz/bdbro_parse <file>`,
then `break __sanitizer::Die` or `break abort`, `run`, `bt`). A failing input
makes a good unit test in `src/test/wallet_sqlite_tests.cpp`.

Fixed findings
--------------

The first smoke runs found three bugs in the wallet code. All three are fixed,
and each minimized input now runs clean with every check on:

- **Verify missed a damaged index.** `CDBEnv::Verify` ran `PRAGMA quick_check`,
  which does not compare the key index with the table. A wallet with two
  swapped bytes in one `ckey` passed it and loaded with one encrypted key
  silently missing. Verify now runs `PRAGMA integrity_check`, which refuses it.
- **Address book ordering.** `CNoDestination`'s `operator<` returned `true`,
  not a valid ordering for `std::map`: `mapAddressBook` lost and leaked nodes
  when a wallet named two invalid addresses. It now returns `false`, and leak
  detection is on for every harness.
- **Empty private key.** `CKey::Load` took `&privkey[0]` of an empty vector
  (undefined behaviour). It now returns `false` first.

Known issues
------------

**Stray WAL files.** A `wallet.dat` whose SQLite header says WAL mode (bytes
18 and 19 set to 2) makes SQLite create `wallet.dat-shm` and `wallet.dat-wal`
beside it when it is opened, read-only included, and they are left there even
when `-salvagewallet` then fails. They are harmless, but a failed recovery
should leave the directory as it found it. `wallet_recover` ignores those two
names unless it runs with `BOB_FUZZ_STRICT_FILES=1`.
