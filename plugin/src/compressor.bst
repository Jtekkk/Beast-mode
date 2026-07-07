# ============================================================================
# Beast Compressor — a feed-forward dynamic-range compressor.
#
# This is the "plugin": all signal processing is Beast. It reads a parameter
# header and an interleaved sample stream on stdin, and writes the processed
# stream on stdout. A host (plugin/wav.py) handles only the WAV binary
# framing — exactly the boundary a real VST plugin sits behind.
#
# Design: log-domain detector -> static soft-knee gain computer -> attack/
# release smoothing of the gain reduction -> makeup gain. Stereo is linked
# (detection uses the peak across channels) so the stereo image doesn't shift.
# Formulas follow Reiss & McPherson, "Digital Dynamic Range Compressor
# Design — A Tutorial and Analysis".
#
# stdin protocol (one value per line):
#   threshold_db  ratio  knee_db  attack_ms  release_ms  makeup_db
#   detector      # "peak" or "rms"
#   sample_rate   channels
#   <interleaved samples, one per line>
# stdout: sample_rate, channels, then the processed interleaved samples.
# ============================================================================

fn lin_to_db(x) { return 20 * log10(x + EPS) }
fn db_to_lin(db) { return pow10(db / 20) }

# Static compression curve: given an input level in dB, return the gain
# reduction (<= 0 dB) the curve asks for, with a soft knee of width knee_db.
fn compute_reduction(xdb, threshold_db, ratio, knee_db) {
    let over = xdb - threshold_db
    if 2 * over < -knee_db {
        return 0                                  # below knee: unity gain
    }
    if 2 * abs(over) <= knee_db {                 # inside the soft knee
        let t = over + knee_db / 2
        return (1 / ratio - 1) * t * t / (2 * knee_db)
    }
    return (threshold_db + over / ratio) - xdb    # above knee: full ratio
}

# Build a stateful compressor. The returned closure processes one frame (an
# array of channel samples) and returns the processed frame, holding the
# envelope and RMS state across calls — one DSP object, just like a plugin
# instance.
fn make_compressor(sr, threshold_db, ratio, knee_db, attack_ms, release_ms,
                   makeup_db, detector) {
    let atk = exp(-1 / (0.001 * attack_ms * sr))
    let rel = exp(-1 / (0.001 * release_ms * sr))
    # RMS averaging time constant (~ the attack window) for the rms detector.
    let rmsCoef = exp(-1 / (0.001 * fmax(attack_ms, 1.0) * sr))
    let useRms = detector == "rms"

    let env = 0.0      # smoothed gain reduction in dB (<= 0)
    let ms = 0.0       # running mean-square for the rms detector

    return fn(frame) {
        # --- detector: linked level across all channels ---
        let peak = 0.0
        for c in 0..len(frame) {
            let a = abs(frame[c])
            if a > peak { peak = a }
        }
        let level = peak
        if useRms {
            ms = rmsCoef * ms + (1 - rmsCoef) * peak * peak
            level = sqrt(ms)
        }
        let xdb = lin_to_db(level)

        # --- static gain computer ---
        let target = compute_reduction(xdb, threshold_db, ratio, knee_db)

        # --- attack/release ballistics on the gain reduction ---
        if target < env {
            env = atk * env + (1 - atk) * target   # more reduction: attack
        } else {
            env = rel * env + (1 - rel) * target   # less reduction: release
        }

        let gain = db_to_lin(env + makeup_db)
        let out = []
        for c in 0..len(frame) {
            push(out, frame[c] * gain)
        }
        return out
    }
}

# ---------------------------------------------------------------- driver ---

fn readNum() {
    let line = readline()
    if line == nil { return nil }
    return num(line)
}

let threshold_db = readNum()
let ratio = readNum()
let knee_db = readNum()
let attack_ms = readNum()
let release_ms = readNum()
let makeup_db = readNum()
let detector = readline()
let sr = readNum()
let channels = readNum()

assert(sr != nil and channels != nil, "malformed audio header on stdin")
let ch = floor(channels)

# Echo the audio header for the downstream WAV writer.
print(str(floor(sr)))
print(str(ch))

let comp = make_compressor(sr, threshold_db, ratio, knee_db, attack_ms,
                           release_ms, makeup_db, detector)

# Stream frames: read `ch` interleaved samples, process, write them back.
let frame = []
for c in 0..ch { push(frame, 0) }

let line = readline()
while line != nil {
    let x = num(line)
    if x != nil {
        frame[0] = x
        let k = 1
        while k < ch {
            let n = num(readline())
            frame[k] = n or 0
            k += 1
        }
        let y = comp(frame)
        for c in 0..ch {
            print(str(y[c]))
        }
    }
    line = readline()
}
