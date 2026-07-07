let xs = [-7, -3.5, -1, -0.1, 0, 0.1, 0.5, 1, 2, 3.5, 6.9]
for i in 0..len(xs) {
    let x = xs[i]
    print("exp " + str(x) + " " + str(exp(x)))
}
let ps = [0.0000001, 0.001, 0.5, 1, 2, 10, 100, 1000, 32768]
for i in 0..len(ps) {
    let x = ps[i]
    print("ln " + str(x) + " " + str(ln(x)))
    print("log10 " + str(x) + " " + str(log10(x)))
}
let ds = [-60, -40, -20, -12, -6, -3, 0, 6, 12]
for i in 0..len(ds) {
    let x = ds[i]
    print("pow10 " + str(x / 20) + " " + str(pow10(x / 20)))
}
let ang = [0, 0.5, 1, 1.5708, 3.14159, -1, 4.5, 6.0]
for i in 0..len(ang) {
    let x = ang[i]
    print("sin " + str(x) + " " + str(sin(x)))
}
