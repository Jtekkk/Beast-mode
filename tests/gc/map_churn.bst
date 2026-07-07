# Repeatedly build and discard maps; keep a running checksum.
fn wordcount(words) {
    let counts = {}
    for i in 0..len(words) {
        let w = words[i]
        counts[w] = get(counts, w, 0) + 1
    }
    return counts
}
let words = []
for i in 0..200 {
    push(words, "w" + str(i % 20))
}
let sum = 0
for round in 0..30 {
    let c = wordcount(words)
    let ks = keys(c)
    for i in 0..len(ks) {
        sum += c[ks[i]]
    }
}
print(sum)                # expect: 6000
