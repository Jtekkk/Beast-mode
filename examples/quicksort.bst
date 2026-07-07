#!/usr/bin/env beast
# Recursive quicksort over an array of numbers.
fn quicksort(a) {
    if len(a) <= 1 { return a }
    let pivot = a[0]
    let less = []
    let more = []
    for i in 1..len(a) {
        if a[i] < pivot {
            push(less, a[i])
        } else {
            push(more, a[i])
        }
    }
    let result = quicksort(less)
    push(result, pivot)
    let sortedMore = quicksort(more)
    for i in 0..len(sortedMore) {
        push(result, sortedMore[i])
    }
    return result
}
print(quicksort([5, 2, 8, 1, 9, 3, 7, 4, 6]))
