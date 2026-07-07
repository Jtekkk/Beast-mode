#!/bin/sh
# Beast test runner. Usage: tests/run_tests.sh [path/to/beast]
#
# Expectation comments inside .bst test files:
#   # expect: TEXT                  stdout line, in order
#   # expect compile error: TEXT    exit 65 and TEXT appears on stderr
#   # expect runtime error: TEXT    exit 70 and TEXT appears on stderr

BIN=${1:-build/beast}
if [ ! -x "$BIN" ]; then
    echo "error: interpreter '$BIN' not found (run 'make' first)" >&2
    exit 1
fi

ROOT=$(dirname "$0")
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

pass=0
fail=0

for t in $(find "$ROOT" -name '*.bst' | sort); do
    grep 'expect compile error:' "$t" | sed 's/.*expect compile error: *//' \
        >"$TMP/cerr.want"
    grep 'expect runtime error:' "$t" | sed 's/.*expect runtime error: *//' \
        >"$TMP/rerr.want"
    grep '# expect: ' "$t" | sed 's/.*# expect: //' >"$TMP/out.want"

    "$BIN" "$t" >"$TMP/out.got" 2>"$TMP/err.got"
    code=$?
    ok=1
    reason=""

    if [ -s "$TMP/cerr.want" ]; then
        if [ "$code" -ne 65 ]; then
            ok=0
            reason="expected exit 65 (compile error), got $code"
        else
            while IFS= read -r want; do
                if ! grep -qF "$want" "$TMP/err.got"; then
                    ok=0
                    reason="missing compile error: $want"
                fi
            done <"$TMP/cerr.want"
        fi
    elif [ -s "$TMP/rerr.want" ]; then
        if [ "$code" -ne 70 ]; then
            ok=0
            reason="expected exit 70 (runtime error), got $code"
        else
            while IFS= read -r want; do
                if ! grep -qF "$want" "$TMP/err.got"; then
                    ok=0
                    reason="missing runtime error: $want"
                fi
            done <"$TMP/rerr.want"
        fi
    else
        if [ "$code" -ne 0 ]; then
            ok=0
            reason="expected exit 0, got $code"
        fi
    fi

    if [ $ok -eq 1 ] && ! cmp -s "$TMP/out.want" "$TMP/out.got"; then
        ok=0
        reason="stdout mismatch"
    fi

    if [ $ok -eq 1 ]; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        echo "FAIL $t ($reason)"
        if [ "$reason" = "stdout mismatch" ]; then
            echo "  --- expected ---"
            sed 's/^/  /' "$TMP/out.want"
            echo "  --- actual ---"
            sed 's/^/  /' "$TMP/out.got"
        else
            sed 's/^/  stderr: /' "$TMP/err.got" | head -4
        fi
    fi
done

echo "$pass passed, $fail failed"
[ $fail -eq 0 ]
