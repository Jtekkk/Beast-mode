let i = 0
let total = 0
while i < 5 {
    i += 1
    total += i
}
print(total)              # expect: 15
# break and continue
let n = 0
let odds = 0
while true {
    n += 1
    if n > 10 { break }
    if n % 2 == 0 { continue }
    odds += 1
}
print(odds)               # expect: 5
# nested loops: break only exits the inner one
let count = 0
let a = 0
while a < 3 {
    a += 1
    let b = 0
    while true {
        b += 1
        if b == 2 { break }
        count += 1
    }
}
print(count)              # expect: 3
