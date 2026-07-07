fn build(n) {
    let parts = []
    for i in 0..n {
        push(parts, "item-" + str(i))
    }
    return len(parts)
}
let total = 0
for round in 0..2000 {
    total = build(1000)
}
print(total)
