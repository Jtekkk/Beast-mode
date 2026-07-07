# Closures keep their captured environment alive across collections.
fn make_adders(n) {
    let adders = []
    for i in 0..n {
        let base = i * 100
        push(adders, fn(x) { return base + x })
    }
    return adders
}
let adders = make_adders(50)
# Allocate garbage to trigger collection, then use the closures.
let junk = []
for i in 0..500 {
    push(junk, "garbage" + str(i))
}
let total = 0
for i in 0..len(adders) {
    let f = adders[i]
    total += f(1)
}
print(total)              # expect: 122550
