def build_words(n):
    return ["word" + str(i % 500) for i in range(n)]
words = build_words(20000)
checksum = 0
for _ in range(30):
    counts = {}
    for w in words:
        counts[w] = counts.get(w, 0) + 1
    for k in counts:
        checksum += counts[k]
print(checksum)
