# Allocate a lot of short-lived objects to force many collections.
fn build(n) {
    let a = []
    for i in 0..n {
        push(a, [i, i * 2, "item" + str(i)])
    }
    return a
}
let total = 0
for round in 0..50 {
    let a = build(100)
    total += len(a)
}
print(total)              # expect: 5000
