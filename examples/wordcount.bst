#!/usr/bin/env beast
# Count word frequencies and print the ones seen more than once.
let text = ["the", "beast", "runs", "the", "race", "the", "beast", "wins"]
let counts = {}
for i in 0..len(text) {
    let w = text[i]
    counts[w] = get(counts, w, 0) + 1
}
let words = keys(counts)
for i in 0..len(words) {
    let w = words[i]
    if counts[w] > 1 {
        print(w + " appears " + str(counts[w]) + " times")
    }
}
