#!/usr/bin/env bash
#
# Run every test suite in this tree, in one command.
#
#   ./qa/run-all.sh [path/to/src]
#
# There is no CI here on purpose (Forgejo Actions is off, GitHub is a push
# mirror), so this is the "run it before a release" option made real. It exits
# non-zero on any UNEXPECTED failure. The four functional tests that are known
# to fail are listed below by name rather than skipped, so the list stays
# visible and shrinks as they are fixed.
#
# Why those four fail: three of them mine 50-100 blocks in a single RPC call,
# and regtest slows from instant to about 33 s/block once the difficulty
# retarget engages above height 10. The fourth, getblocktemplate_proposals,
# hits the template-vs-recomputed-target trap written up in qa/cltv/README.md.
set -u

SRC="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../src" && pwd)}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DOBBSCOIND="$SRC/dobbscoind"
CLI="$SRC/dobbscoin-cli"

KNOWN_FAIL="bipdersig.py getblocktemplate_proposals.py wallet.py walletbackup.py"

for b in "$DOBBSCOIND" "$CLI"; do
    [[ -x "$b" ]] || { echo "not built: $b"; exit 2; }
done

pass=0; fail=0; expected=0
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
for t in "$HERE"/rpc-tests/*.py; do
    name=$(basename "$t")
    case $name in netutil.py|util.py|test_framework.py) continue;; esac
    log=/tmp/run-all-$name.log
    timeout 300 python3 "$t" --srcdir "$SRC" > "$log" 2>&1
    if grep -q "Tests successful" "$log"; then
        report "$name" PASS
    elif [[ " $KNOWN_FAIL " == *" $name "* ]]; then
        report "$name" XFAIL
    else
        report "$name" FAIL
        grep -iE "Unexpected exception|Assertion failed|Error:" "$log" | head -3
    fi
    pkill -f '[d]atadir=/tmp/test' 2>/dev/null
done

echo
echo "  passed $pass   failed $fail   known failures $expected"
[[ $fail -eq 0 ]]
