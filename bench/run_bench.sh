#!/bin/sh
# Beast benchmark harness. Usage: bench/run_bench.sh [path/to/beast]
#
# Runs each paired program under Beast and python3, takes the best wall-clock
# of N runs for each, and prints a comparison table. Every number in the
# README benchmark section comes from this script - none are hand-typed.

BIN=${1:-build/beast}
RUNS=${BENCH_RUNS:-3}
ROOT=$(dirname "$0")
PROG="$ROOT/programs"

if [ ! -x "$BIN" ]; then
    echo "error: interpreter '$BIN' not found (run 'make' first)" >&2
    exit 1
fi
PYTHON=$(command -v python3 || command -v python)
if [ -z "$PYTHON" ]; then
    echo "error: python3 not found" >&2
    exit 1
fi

# Portable high-resolution timer: prints elapsed seconds for "$@".
time_cmd() {
    start=$($PYTHON -c 'import time; print(time.monotonic())')
    "$@" >/dev/null 2>&1
    end=$($PYTHON -c 'import time; print(time.monotonic())')
    $PYTHON -c "print(f'{$end - $start:.4f}')"
}

best_of() {
    best=""
    i=0
    while [ $i -lt "$RUNS" ]; do
        t=$(time_cmd "$@")
        if [ -z "$best" ] || $PYTHON -c "exit(0 if $t < $best else 1)"; then
            best=$t
        fi
        i=$((i + 1))
    done
    echo "$best"
}

echo "Beast benchmark suite"
echo "  interpreter: $BIN"
echo "  python:      $($PYTHON --version 2>&1)"
echo "  runs:        best of $RUNS"
echo ""
printf "%-14s %12s %12s %10s\n" "benchmark" "beast(s)" "python(s)" "speedup"
printf "%-14s %12s %12s %10s\n" "-------------" "-----------" "-----------" "---------"

for bst in "$PROG"/*.bst; do
    name=$(basename "$bst" .bst)
    py="$PROG/$name.py"
    [ -f "$py" ] || continue

    bt=$(best_of "$BIN" "$bst")
    pt=$(best_of "$PYTHON" "$py")
    speedup=$($PYTHON -c "print(f'{$pt / $bt:.2f}x') if $bt > 0 else print('n/a')")
    printf "%-14s %12s %12s %10s\n" "$name" "$bt" "$pt" "$speedup"
done
