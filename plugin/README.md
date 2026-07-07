# Beast Audio Compressor 🎚️

A working **dynamic-range compressor** whose entire signal path — envelope
detection, the soft-knee gain computer, attack/release ballistics, makeup
gain, and even the transcendental math (`exp`, `log10`, `pow10`) — is written
in [Beast](../README.md), the language in this repo. The only non-Beast piece
is `wav.py`, a ~60-line shim that packs and unpacks WAV bytes. That is exactly
the boundary a real VST/AU plugin sits behind: the host hands the plugin float
buffers, and the plugin does the DSP.

```
$ ./demo.sh
...
================ BEFORE (raw signal) ================
loudness range 21.38 dB   <- compression shrinks this
================ AFTER (compressed) =================
peak          -1 dBFS
loudness range 11.05 dB   <- compression shrinks this
```

The loud sections get pulled toward the quiet ones (21 dB of dynamic range
squeezed to 11 dB), and auto makeup gain lands the peak right at the −1 dBFS
ceiling. That drop is the compressor working.

## Run it

```sh
./demo.sh
```

That builds `beast`, synthesizes a high-dynamic-range test signal, compresses
it, prints before/after measurements and ASCII envelopes, and round-trips a
real 16-bit WAV file through the plugin (`work/input.wav` → `work/output.wav`).

## The plugin boundary

Everything is a plain-text sample stream: `sample_rate`, then `channels`, then
one float sample per line (interleaved). Programs compose with pipes.

```sh
./build.sh                          # concatenate the math prelude into build/

# compress a WAV file:
python3 wav.py to-text in.wav \
  | { printf '%s\n' -20 4 6 5 80 3 peak; cat; } \
  | ../build/beast build/compressor.bst \
  | python3 wav.py from-text out.wav

# or work entirely in Beast, no WAV needed:
../build/beast build/gen_signal.bst \
  | { printf '%s\n' -20 4 6 5 80 3 peak; cat; } \
  | ../build/beast build/compressor.bst \
  | ../build/beast build/analyze.bst
```

The compressor reads its parameters as a header (one per line) ahead of the
audio stream:

| Line | Parameter    | Example | Meaning                              |
|------|--------------|---------|--------------------------------------|
| 1    | threshold_db | `-20`   | Level above which gain reduction starts |
| 2    | ratio        | `4`     | 4:1 — 4 dB in yields 1 dB out above threshold |
| 3    | knee_db      | `6`     | Soft-knee width around the threshold |
| 4    | attack_ms    | `5`     | How fast gain reduction engages      |
| 5    | release_ms   | `80`    | How fast it recovers                 |
| 6    | makeup_db    | `3`     | Output gain to compensate for the reduction |
| 7    | detector     | `peak`  | `peak` or `rms`                      |

(Then `sample_rate`, `channels`, and the samples — supplied by the host or by
`gen_signal.bst`.)

## How the DSP works

A textbook feed-forward compressor (formulas after Reiss & McPherson, *Digital
Dynamic Range Compressor Design — A Tutorial and Analysis*):

1. **Detector.** Per sample, take the peak level across channels (linked, so
   the stereo image doesn't shift), or a smoothed RMS. Convert to dB.
2. **Gain computer.** A static curve maps input level to a target gain
   reduction, with a quadratic soft knee around the threshold.
3. **Ballistics.** The gain reduction is smoothed with separate attack and
   release one-pole coefficients (`exp(-1 / (t · sr))`) — fast to clamp down,
   slower to let go.
4. **Makeup + apply.** Add makeup gain, convert back to linear, multiply.

The returned processor is a Beast **closure** that holds the envelope state, so
one instance is one DSP object — exactly how a plugin keeps per-voice state.

## Files

```
src/prelude.bst      math in pure Beast: exp, ln, log10, pow10, sin, clamp...
src/compressor.bst   THE PLUGIN — detector, gain computer, ballistics, makeup
src/gen_signal.bst   synthesize a loud/quiet test signal (pure Beast)
src/analyze.bst      peak / RMS / crest / loudness-range report (pure Beast)
src/plot.bst         ASCII envelope plot (pure Beast)
src/math_check.bst   validates the prelude against libm
build.sh             concatenate prelude + program -> build/*.bst
wav.py               WAV <-> float-text host shim (the only non-Beast code)
demo.sh              end-to-end demonstration
```

## Notes and honest limitations

- **Why concatenate?** Beast v0.1 has no module system (`import` is a reserved
  word), so `build.sh` prepends the shared math prelude to each program. It's
  the current stand-in for imports.
- **Transcendentals in Beast.** `exp`/`ln`/`log10`/`pow10` are implemented from
  `+ - * /` via range reduction plus short series, and validated to ~1e-14
  relative error against libm (`src/math_check.bst`) — well below audible for
  decibel math.
- **Text-stream I/O** is simple and composable but not real-time; this is an
  offline processor, not a live plugin. Processing is a few seconds for a
  couple of seconds of 44.1 kHz stereo.
- **Loudness range** (P90 − P25 of short-time loudness) follows the spirit of
  EBU R128 LRA; percentiles are used so brief release-tail dips don't distort
  the measurement.
- The compressor never hard-clips; `wav.py` clamps to [−1, 1] only as a final
  safety when writing 16-bit PCM.
