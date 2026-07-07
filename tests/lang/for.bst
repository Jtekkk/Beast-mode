let total = 0
for i in 0..5 {
    total += i
}
print(total)              # expect: 10
# half-open: the limit is excluded
for i in 3..4 {
    print(i)              # expect: 3
}
# empty and reversed ranges run zero times
let hits = 0
for i in 5..5 { hits += 1 }
for i in 9..2 { hits += 1 }
print(hits)               # expect: 0
# non-integer bounds
let steps = 0
for i in 0.5..3 { steps += 1 }
print(steps)              # expect: 3
# reassigning the loop variable doesn't affect iteration
let seen = 0
for i in 0..3 {
    i = 99
    seen += 1
}
print(seen)               # expect: 3
# break / continue
let odds = 0
for i in 0..100 {
    if i % 2 == 0 { continue }
    if i > 8 { break }
    odds += 1
}
print(odds)               # expect: 4
# nested
let grid = 0
for i in 0..3 {
    for j in 0..4 {
        grid += 1
    }
}
print(grid)               # expect: 12
# expressions as bounds
let lo = 2
let hi = 5
let s = 0
for i in lo..hi { s += i }
print(s)                  # expect: 9
