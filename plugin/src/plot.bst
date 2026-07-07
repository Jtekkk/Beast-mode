# ============================================================================
# plot — ASCII envelope of a sample stream, in pure Beast.
#
# Buckets the audio into time columns, takes the peak in each, and draws a
# horizontal dB bar per column. Run it on the signal before and after the
# compressor to *see* the loud sections pulled down.
# ============================================================================

let COLS = 40          # time buckets
let WIDTH = 50         # bar width in characters
let FLOOR_DB = -60.0   # bottom of the dB scale

let sr = num(readline())
let channels = num(readline())

# Collect the per-sample peak across channels into one mono envelope.
let mono = []
let ch = floor(channels)
let line = readline()
while line != nil {
    let x = num(line)
    if x != nil {
        let a = abs(x)
        let k = 1
        while k < ch {
            let n = num(readline())
            if n != nil {
                let an = abs(n)
                if an > a { a = an }
            }
            k += 1
        }
        push(mono, a)
        line = readline()
    } else {
        line = readline()
    }
}

let total = len(mono)
assert(total > 0, "no samples on stdin")
let per = total / COLS

fn bar(level) {
    let db = FLOOR_DB
    if level > EPS { db = 20 * log10(level) }
    let frac = (db - FLOOR_DB) / (0 - FLOOR_DB)
    frac = clamp(frac, 0, 1)
    let n = floor(frac * WIDTH)
    let s = ""
    let i = 0
    while i < WIDTH {
        if i < n { s += "#" } else { s += " " }
        i += 1
    }
    return s + " " + str(floor(db)) + " dB"
}

let c = 0
while c < COLS {
    let start = floor(c * per)
    let stop = floor((c + 1) * per)
    if stop > total { stop = total }
    let peak = 0.0
    let i = start
    while i < stop {
        if mono[i] > peak { peak = mono[i] }
        i += 1
    }
    let t = start / total
    print("|" + bar(peak))
    c += 1
}
