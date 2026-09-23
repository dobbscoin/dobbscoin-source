#!/usr/bin/env bash
#
# Run every test suite in this tree, in one command.
#
#   ./qa/run-all.sh [path/to/src]
#
# There is no CI here on purpose (Forgejo Actions is off, GitHub is a push
# mirror), so this is the "run it before a release" option made real. It exits
# non-zero on any UNEXPECTED failure. Tests that are known to fail are listed by
# name in KNOWN_FAIL rather than skipped, so the list stays visible and shrinks
# as they are fixed.
#
# KNOWN_FAIL is empty and should stay that way. It was four tests until regtest
# stopped retargeting; all four were the same difficulty problem wearing
# different hats, getblocktemplate_proposals' bad-diffbits included.
set -u

SRC="$(cd "${1:-$(dirname "${BASH_SOURCE[0]}")/../src}" && pwd)"  # absolute: the sub-suites cd elsewhere
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DOBBSCOIND="$SRC/dobbscoind"
CLI="$SRC/dobbscoin-cli"

KNOWN_FAIL=""

for b in "$DOBBSCOIND" "$CLI"; do
    [[ -x "$b" ]] || { echo "not built: $b"; exit 2; }
done

pass=0; fail=0; expected=0

# Stop whatever a functional test left running and WAIT until it has exited.
# pkill only sends SIGTERM; a node flushing its chainstate on the way out can
# hold its ports for seconds, and the next test used to start straight away.
reap_test_nodes() {
    pkill -f '[d]atadir=/tmp/test' 2>/dev/null || return 0
    for _ in $(seq 30); do
        pgrep -f '[d]atadir=/tmp/test' >/dev/null || return 0
        sleep 1
    done
    pkill -9 -f '[d]atadir=/tmp/test' 2>/dev/null
    sleep 1
}
report() { # name verdict
    case "$2" in
        PASS) pass=$((pass+1));   printf "  %-40s PASS\n" "$1" ;;
        XFAIL) expected=$((expected+1)); printf "  %-40s FAIL (known)\n" "$1" ;;
        *)    fail=$((fail+1));   printf "  %-40s FAIL\n" "$1" ;;
    esac
}

echo "== unit tests"
if "$SRC/test/test_dobbscoin" > /tmp/run-all-unit.log 2>&1; then
    report "src/test/test_dobbscoin" PASS
else
    report "src/test/test_dobbscoin" FAIL; tail -20 /tmp/run-all-unit.log
fi

echo "== chain harnesses"
for suite in "cltv/run-cltv-chain-tests.sh" \
             "auxpow-smoke/regtest-smoke.sh" \
             "auxpow-smoke/run-parent-pow-tests.sh" \
             "assumevalid/run-assumevalid-tests.sh"; do
    log=/tmp/run-all-$(basename "$(dirname "$suite")")-$(basename "$suite" .sh).log
    if (cd "$HERE/$(dirname "$suite")" && ./"$(basename "$suite")" "$DOBBSCOIND" "$CLI") > "$log" 2>&1; then
        report "qa/$suite" PASS
    else
        report "qa/$suite" FAIL; tail -15 "$log"
    fi
done

echo "== functional tests (qa/rpc-tests)"
# The framework keeps a prebuilt 200-block chain, wallets included, in ./cache and
# reuses it whenever it exists. A cache left by an earlier build is not what this
# build would make: after the SQLite wallet landed, a cache from the Berkeley DB
# build fed walletmigration.py a wallet it then migrated. So every run starts from
# a fresh working directory and builds its own cache once, for all the tests.
WORK=$(mktemp -d /tmp/run-all-cwd.XXXXXX)
trap 'rm -rf "$WORK"' EXIT
for t in "$HERE"/rpc-tests/*.py; do
    name=$(basename "$t")
    case $name in netutil.py|util.py|test_framework.py) continue;; esac
    log=/tmp/run-all-$name.log
    (cd "$WORK" && timeout 300 python3 "$t" --srcdir "$SRC") > "$log" 2>&1
    if grep -q "Tests successful" "$log"; then
        report "$name" PASS
    elif [[ " $KNOWN_FAIL " == *" $name "* ]]; then
        report "$name" XFAIL
    else
        report "$name" FAIL
        grep -iE "Unexpected exception|Assertion failed|Error:" "$log" | head -3
    fi
    reap_test_nodes
done

echo
echo "  passed $pass   failed $fail   known failures $expected"
[[ $fail -eq 0 ]]
