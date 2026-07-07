#!/usr/bin/env beast
# Sieve of Eratosthenes: primes below 50.
fn primes_below(limit) {
    let sieve = []
    for i in 0..limit { push(sieve, true) }
    let out = []
    for n in 2..limit {
        if sieve[n] {
            push(out, n)
            let multiple = n * n
            while multiple < limit {
                sieve[multiple] = false
                multiple += n
            }
        }
    }
    return out
}
print(primes_below(50))
