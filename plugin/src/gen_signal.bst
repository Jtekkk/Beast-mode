# ============================================================================
# gen_signal — synthesize a high-dynamic-range test signal, in pure Beast.
#
# Alternating LOUD and QUIET sustained sections (a loud phrase, then a quiet
# one, repeating) — musical macro-dynamics, which is exactly what a compressor
# is built to tame. Each loud section also opens with a short transient so
# peak-limiting behaviour is visible too. Emits the standard stream:
# sample_rate, channels, then interleaved samples. Stereo.
# ============================================================================

let SR = 44100
let CHANNELS = 2
let DURATION = 2.4
let N = floor(SR * DURATION)

print(str(SR))
print(str(CHANNELS))

let toneInc = TWO_PI * 220.0 / SR     # 220 Hz musical tone
let hitInc = TWO_PI * 660.0 / SR      # transient tone at each onset
let section = 0.3                     # seconds per loud/quiet section
let loudAmp = 0.70
let quietAmp = 0.06
let hitTau = 0.03                     # onset transient decay

let tonePhase = 0.0
let hitPhase = 0.0

let i = 0
while i < N {
    let t = i / SR
    # Which section are we in? Even = loud, odd = quiet.
    let sectionIndex = floor(t / section)
    let loud = sectionIndex - 2 * floor(sectionIndex / 2) == 0
    let amp = quietAmp
    if loud { amp = loudAmp }

    let tone = amp * sin(tonePhase)
    tonePhase += toneInc
    if tonePhase > PI { tonePhase -= TWO_PI }

    # A short transient at the very start of each loud section.
    let sinceSection = t - sectionIndex * section
    let hit = 0.0
    if loud {
        hit = 0.25 * exp(-sinceSection / hitTau) * sin(hitPhase)
    }
    hitPhase += hitInc
    if hitPhase > PI { hitPhase -= TWO_PI }

    let left = clamp(tone + hit, -1, 1)
    let right = clamp(tone * 0.95 + hit, -1, 1)
    print(str(left))
    print(str(right))
    i += 1
}
