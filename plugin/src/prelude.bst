# Beast math prelude — transcendental functions in pure Beast.
#
# Beast v0.1 ships only floor() and sqrt(); everything a compressor needs for
# decibel math (exp, ln, log10, pow10) is built here from +, -, *, / and
# comparisons. Because `import` is a reserved word (no module system yet),
# this block is concatenated ahead of each program that uses it — see
# build.sh. Accuracy is validated against libm in plugin/src/math_check.bst.

let LN2 = 0.6931471805599453
let LN10 = 2.302585092994046
let PI = 3.141592653589793
let TWO_PI = 6.283185307179586
let EPS = 0.0000001   # -140 dBFS floor; keeps log10 finite at digital silence

fn abs(x) {
    if x < 0 { return -x }
    return x
}

fn fmax(a, b) {
    if a > b { return a }
    return b
}

fn fmin(a, b) {
    if a < b { return a }
    return b
}

fn clamp(x, lo, hi) {
    if x < lo { return lo }
    if x > hi { return hi }
    return x
}

# e^x via range reduction: x = k*ln2 + r with r in [-ln2/2, ln2/2], then
# e^x = 2^k * e^r, and e^r from its Taylor series (r is small so it converges
# in a handful of terms).
fn exp(x) {
    if x != x { return x }        # NaN in, NaN out
    if x > 700 { return 1e308 * 10 }   # overflow -> inf
    if x < -700 { return 0 }
    let k = floor(x / LN2 + 0.5)
    let r = x - k * LN2
    let term = 1.0
    let sum = 1.0
    let n = 1
    while n < 18 {
        term = term * r / n
        sum += term
        n += 1
    }
    # multiply by 2^k (k is small for all in-range audio values)
    let p = 1.0
    let kk = k
    while kk > 0 { p *= 2; kk -= 1 }
    while kk < 0 { p /= 2; kk += 1 }
    return sum * p
}

# Natural log. Reduce x = m * 2^e with m in [1, 2), then use the fast-
# converging series ln(m) = 2*atanh(s), s = (m-1)/(m+1).
fn ln(x) {
    if x <= 0 { return -1e308 * 10 }   # -inf; callers add an epsilon
    let e = 0
    let m = x
    while m >= 2 { m /= 2; e += 1 }
    while m < 1 { m *= 2; e -= 1 }
    let s = (m - 1) / (m + 1)
    let s2 = s * s
    let term = s
    let sum = s
    let n = 3
    while n < 30 {
        term = term * s2
        sum += term / n
        n += 2
    }
    return e * LN2 + 2 * sum
}

fn log10(x) { return ln(x) / LN10 }

fn pow10(x) { return exp(x * LN10) }

fn powf(base, e) {
    if base <= 0 { return 0 }
    return exp(e * ln(base))
}

# sin via range reduction to [-PI, PI] then Taylor series. Callers that
# accumulate phase should keep it wrapped to avoid large reductions.
fn sin(x) {
    let y = x
    while y > PI { y -= TWO_PI }
    while y < -PI { y += TWO_PI }
    let y2 = y * y
    let term = y
    let sum = y
    let n = 1
    while n < 11 {
        let d = (2 * n) * (2 * n + 1)
        term = -term * y2 / d
        sum += term
        n += 1
    }
    return sum
}
