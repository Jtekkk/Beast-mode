#!/bin/sh
# Validate the pure-Beast math prelude against the system libm.
# Runs math_check.bst (Beast) and compares every value to Python's math module,
# reporting the worst relative error.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
make -C "$ROOT" >/dev/null
"$HERE/build.sh" >/dev/null
OUT=$(mktemp)
trap 'rm -f "$OUT"' EXIT
"$ROOT/build/beast" "$HERE/build/math_check.bst" > "$OUT"
# The Python script comes from the heredoc (stdin), so pass the data by path.
python3 - "$OUT" <<'PY'
import sys, math
ref = {'exp': math.exp, 'ln': math.log, 'log10': math.log10,
       'pow10': lambda v: 10 ** v, 'sin': math.sin}
worst = {}
for line in open(sys.argv[1]):
    fn, x, got = line.split()
    x, got = float(x), float(got)
    r = ref[fn](x)
    rel = abs(got - r) / max(abs(r), 1e-9)
    worst[fn] = max(worst.get(fn, 0), rel)
for fn, r in sorted(worst.items()):
    print(f"  {fn:7s} max relative error {r:.2e}")
w = max(worst.values())
print(f"\n  WORST {w:.2e}  ->  {'PASS' if w < 1e-5 else 'FAIL'}")
sys.exit(0 if w < 1e-5 else 1)
PY
