fn count_primes(limit) {
    let sieve = []
    for i in 0..limit { push(sieve, true) }
    let count = 0
    for n in 2..limit {
        if sieve[n] {
            count += 1
            let m = n * n
            while m < limit {
                sieve[m] = false
                m += n
            }
        }
    }
    return count
}
let total = 0
for round in 0..200 {
    total = count_primes(10000)
}
print(total)
