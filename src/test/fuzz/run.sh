#!/bin/sh
# Copyright (c) 2026 The Dobbscoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# Run one fuzz harness with the settings doc/fuzzing.md describes.
#
#   src/test/fuzz/run.sh <target> [more libFuzzer options]
#
# Environment:
#   BOB_FUZZ_BUILD  build directory     (default: build-fuzz at the top of this tree)
#   BOB_FUZZ_OUT    results directory   (default: $HOME/bob-fuzz-out)
#   FUZZ_SECONDS    how long to run     (default: 36000, ten hours)
#
# The growing corpus goes to $BOB_FUZZ_OUT/<target>/corpus, crashes to
# $BOB_FUZZ_OUT/<target>/crashes; the checked-in seeds in
# src/test/fuzz/corpus/<target> are read but never written.
set -eu

target=${1:?usage: run.sh <target> [libFuzzer options]}
shift
top=$(cd "$(dirname "$0")/../../.." && pwd)
build=${BOB_FUZZ_BUILD:-$top/build-fuzz}
out=${BOB_FUZZ_OUT:-$HOME/bob-fuzz-out}/$target
bin=$build/src/test/fuzz/$target
seeds=$top/src/test/fuzz/corpus/$target
[ -x "$bin" ] || { echo "no $bin: build with -DBUILD_FUZZ=ON first (doc/fuzzing.md)" >&2; exit 1; }
[ -d "$seeds" ] || { echo "no seeds at $seeds" >&2; exit 1; }
mkdir -p "$out/corpus" "$out/crashes"

case $target in
  wallet_load)
    max_len=8192
    # Known, reported: CNoDestination's operator< makes mapAddressBook leak
    # nodes whenever two records name invalid addresses. Leak reports would
    # stop every run at once, so they are off here; ASan and UBSan stay on.
    leaks=0 ;;
  wallet_recover)
    max_len=32768
    leaks=0 ;;
  *)
    max_len=32768
    leaks=1 ;;
esac

ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=$leaks:abort_on_error=1:symbolize=1}
UBSAN_OPTIONS=${UBSAN_OPTIONS:-print_stacktrace=1:halt_on_error=1:suppressions=$top/src/test/fuzz/ubsan.supp}
export ASAN_OPTIONS UBSAN_OPTIONS

exec "$bin" \
  -max_total_time="${FUZZ_SECONDS:-36000}" \
  -rss_limit_mb=2048 \
  -timeout=10 \
  -max_len="$max_len" \
  -detect_leaks="$leaks" \
  -print_final_stats=1 \
  -artifact_prefix="$out/crashes/" \
  "$@" \
  "$out/corpus" "$seeds"
