# and/or short-circuit and yield the deciding operand.
print(1 and 2)            # expect: 2
print(nil and 2)          # expect: nil
print(false and 2)        # expect: false
print(1 or 2)             # expect: 1
print(nil or 2)           # expect: 2
print(false or "x")       # expect: x
print(nil or false)       # expect: false
# ternary idiom
let n = 7
print(n > 5 and "big" or "small")   # expect: big
# short-circuit: rhs must not run
let hits = 0
fn bump() { hits += 1; return true }
let r = false and bump()
r = true or bump()
print(hits)               # expect: 0
r = true and bump()
r = false or bump()
print(hits)               # expect: 2
print(r)                  # expect: true
