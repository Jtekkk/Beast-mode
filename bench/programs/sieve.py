def count_primes(limit):
    sieve = [True] * limit
    count = 0
    for n in range(2, limit):
        if sieve[n]:
            count += 1
            m = n * n
            while m < limit:
                sieve[m] = False
                m += n
    return count
total = 0
for _ in range(200):
    total = count_primes(10000)
print(total)
