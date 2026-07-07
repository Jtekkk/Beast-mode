# ============================================================================
# analyze — measure a sample stream's dynamics, in pure Beast.
#
# Reads the standard stream (sample_rate, channels, samples) and reports peak
# level, RMS level, and crest factor (peak-to-RMS, in dB). Compression lowers
# the crest factor: that drop is the quantitative proof the plugin works.
# ============================================================================

fn round2(x) {
    let s = 1
    if x < 0 { s = -1 }
    return s * floor(abs(x) * 100 + 0.5) / 100
}

fn db_str(lin) {
    if lin <= EPS { return "-inf" }
    return str(round2(20 * log10(lin)))
}

let sr = num(readline())
let channels = num(readline())
let ch = floor(channels)

# Short-time RMS window (~46 ms) for the dynamic-range measurement.
let winFrames = floor(sr * 0.046)
if winFrames < 1 { winFrames = 1 }

let peak = 0.0
let sumsq = 0.0
let count = 0

# Rolling window state (mono-summed power) and the collected window levels.
let winSumsq = 0.0
let winCount = 0
let wins = []

fn windowDb(sumsq, n) { return 20 * log10(sqrt(sumsq / n) + EPS) }

# Insertion sort (small window count) and a percentile helper.
fn sortAsc(a) {
    let i = 1
    while i < len(a) {
        let v = a[i]
        let j = i - 1
        while j >= 0 and a[j] > v {
            a[j + 1] = a[j]
            j -= 1
        }
        a[j + 1] = v
        i += 1
    }
    return a
}

fn percentile(sorted, p) {
    let idx = floor(p / 100 * (len(sorted) - 1) + 0.5)
    return sorted[clamp(idx, 0, len(sorted) - 1)]
}

let line = readline()
while line != nil {
    let x = num(line)
    if x != nil {
        # Fold a full frame into one mono power sample for the envelope.
        let framePow = x * x
        let a = abs(x)
        let k = 1
        while k < ch {
            let n = num(readline())
            if n != nil {
                framePow += n * n
                if abs(n) > a { a = abs(n) }
            }
            k += 1
        }
        if a > peak { peak = a }
        sumsq += framePow
        count += 1

        winSumsq += framePow
        winCount += 1
        if winCount >= winFrames {
            push(wins, windowDb(winSumsq, winCount * ch))
            winSumsq = 0.0
            winCount = 0
        }
        line = readline()
    } else {
        line = readline()
    }
}

assert(count > 0, "no samples on stdin")
let rms = sqrt(sumsq / (count * ch))
let crest = 20 * log10(peak + EPS) - 20 * log10(rms + EPS)

sortAsc(wins)
# Loudness range: high vs low percentile of short-time loudness (EBU R128
# LRA uses P95-P10). Percentiles ignore the brief release-tail dips that a
# raw min/max would catch, so this reflects sustained macro-dynamics.
let hi = percentile(wins, 90)
let lo = percentile(wins, 25)
let lra = hi - lo

print("samples       " + str(count) + " frames (" + str(ch) + " ch @ " +
      str(floor(sr)) + " Hz)")
print("peak          " + db_str(peak) + " dBFS")
print("rms           " + db_str(rms) + " dBFS")
print("crest factor  " + str(round2(crest)) + " dB")
print("loud (P90)    " + str(round2(hi)) + " dBFS")
print("quiet (P25)   " + str(round2(lo)) + " dBFS")
print("loudness range " + str(round2(lra)) + " dB   <- compression shrinks this")
