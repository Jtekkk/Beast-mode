#!/usr/bin/env beast
# ASCII Mandelbrot set — a compact numeric workout.
let chars = " .:-=+*#%@"
for py in 0..24 {
    let y = (py / 24.0) * 2.0 - 1.0
    let line = ""
    for px in 0..64 {
        let x = (px / 64.0) * 3.0 - 2.0
        let zr = 0.0
        let zi = 0.0
        let iter = 0
        while iter < 90 {
            let zr2 = zr * zr - zi * zi + x
            zi = 2.0 * zr * zi + y
            zr = zr2
            if zr * zr + zi * zi > 4.0 { break }
            iter += 1
        }
        let idx = floor(iter / 90.0 * 9)
        line += chars[idx]
    }
    print(line)
}
