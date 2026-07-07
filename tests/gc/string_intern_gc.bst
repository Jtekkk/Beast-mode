# Build many strings, keep some, drop most.
let kept = []
for i in 0..1000 {
    let s = "prefix-" + str(i % 10)
    if i % 100 == 0 {
        push(kept, s)
    }
}
print(len(kept))          # expect: 10
# All the kept strings with the same suffix are interned to one value.
print(kept[0] == kept[0])  # expect: true
