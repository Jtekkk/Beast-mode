fn build_words(n) {
    let words = []
    for i in 0..n {
        push(words, "word" + str(i % 500))
    }
    return words
}
let words = build_words(20000)
let checksum = 0
for round in 0..30 {
    let counts = {}
    for i in 0..len(words) {
        let w = words[i]
        counts[w] = get(counts, w, 0) + 1
    }
    let ks = keys(counts)
    for i in 0..len(ks) {
        checksum += counts[ks[i]]
    }
}
print(checksum)
