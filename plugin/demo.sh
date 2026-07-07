#!/bin/sh
# End-to-end demo of the Beast audio compressor.
#
#   1. synthesize a high-dynamic-range test signal (pure Beast)
#   2. run it through the compressor (pure Beast) with auto makeup gain
#   3. measure the loudness range before and after (pure Beast)
#   4. draw ASCII envelopes before and after (pure Beast)
#   5. round-trip a real 16-bit WAV file through the plugin (wav.py host)
#
# The only non-Beast component is wav.py, which just packs/unpacks WAV bytes.
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
BEAST="$ROOT/build/beast"
WORK="$HERE/work"
mkdir -p "$WORK"

# --- Compressor settings (the "knobs") ---------------------------------------
THRESHOLD=-20   # dBFS
RATIO=4         # 4:1
KNEE=6          # dB soft knee
ATTACK=5        # ms
RELEASE=80      # ms
DETECTOR=peak   # peak | rms
CEILING=-1      # dBFS output ceiling for auto makeup

echo "==> building beast + plugin"
make -C "$ROOT" >/dev/null
"$HERE/build.sh" >/dev/null

run_comp() { # makeup_db  <in.txt >out.txt
    { printf '%s\n' "$THRESHOLD" "$RATIO" "$KNEE" "$ATTACK" "$RELEASE" "$1" \
        "$DETECTOR"; cat "$2"; } | "$BEAST" "$HERE/build/compressor.bst"
}

echo "==> synthesizing test signal"
"$BEAST" "$HERE/build/gen_signal.bst" > "$WORK/raw.txt"

echo "==> auto makeup gain (target ceiling ${CEILING} dBFS)"
run_comp 0 "$WORK/raw.txt" > "$WORK/comp0.txt"
PEAK0=$("$BEAST" "$HERE/build/analyze.bst" < "$WORK/comp0.txt" \
        | awk '/^peak/ {print $2}')
MAKEUP=$(python3 -c "print(round($CEILING - ($PEAK0), 2))")
echo "    makeup = ${MAKEUP} dB"
run_comp "$MAKEUP" "$WORK/raw.txt" > "$WORK/comp.txt"

echo ""
echo "================ BEFORE (raw signal) ================"
"$BEAST" "$HERE/build/analyze.bst" < "$WORK/raw.txt"
echo ""
echo "================ AFTER (compressed) ================="
"$BEAST" "$HERE/build/analyze.bst" < "$WORK/comp.txt"

echo ""
echo "================ ENVELOPE: before ==================="
"$BEAST" "$HERE/build/plot.bst" < "$WORK/raw.txt"
echo ""
echo "================ ENVELOPE: after ===================="
"$BEAST" "$HERE/build/plot.bst" < "$WORK/comp.txt"

echo ""
echo "==> real WAV round-trip via the wav.py host"
python3 "$HERE/wav.py" from-text "$WORK/input.wav"  < "$WORK/raw.txt"
python3 "$HERE/wav.py" to-text   "$WORK/input.wav" \
    | { printf '%s\n' "$THRESHOLD" "$RATIO" "$KNEE" "$ATTACK" "$RELEASE" \
          "$MAKEUP" "$DETECTOR"; cat; } \
    | "$BEAST" "$HERE/build/compressor.bst" \
    | python3 "$HERE/wav.py" from-text "$WORK/output.wav"
echo "    wrote $WORK/input.wav and $WORK/output.wav"
python3 - "$WORK/input.wav" "$WORK/output.wav" <<'PY'
import sys, wave, array, math
for path in sys.argv[1:3]:
    w = wave.open(path, "rb")
    s = array.array("h"); s.frombytes(w.readframes(w.getnframes()))
    peak = max(abs(v) for v in s) / 32768.0
    rms = math.sqrt(sum(v * v for v in s) / len(s)) / 32768.0
    db = lambda v: 20 * math.log10(v + 1e-9)
    print(f"    {path.split('/')[-1]:11s} peak {db(peak):6.1f} dBFS  "
          f"rms {db(rms):6.1f} dBFS")
PY
echo ""
echo "Done. The loudness range shrinks and the loud sections are pulled"
echo "toward the quiet ones — that is the compressor working."
