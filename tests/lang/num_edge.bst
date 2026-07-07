# NaN prints without a sign, and NaN is never equal to itself.
print(0 / 0)              # expect: nan
print(sqrt(-1))           # expect: nan
print(0 / 0 == 0 / 0)     # expect: false
print(1 / 0)              # expect: inf
print(-1 / 0)             # expect: -inf
# -0 prints as 0 and is the same map key as 0.
print(-0)                 # expect: 0
print(0 == -0)            # expect: true
let m = {}
m[-0] = "zero"
print(m[0])               # expect: zero
print(len(m))             # expect: 1
# Exactness holds to 2^53; just past it, the odd value rounds.
print(9007199254740992)   # expect: 9007199254740992
print(9007199254740993)   # expect: 9007199254740992
# floor of a large-ish integer stays integer-formatted.
print(1000000 * 1000000)  # expect: 1000000000000
