let x = 10
x += 5
print(x)                  # expect: 15
x -= 3
print(x)                  # expect: 12
x *= 2
print(x)                  # expect: 24
x /= 4
print(x)                  # expect: 6
# on array elements
let a = [1, 2]
a[0] += 10
print(a[0])               # expect: 11
a[1] *= 5
print(a[1])               # expect: 10
# on map fields via dot and index
let m = { count: 0 }
m.count += 2
m["count"] += 3
print(m.count)            # expect: 5
# container and index are evaluated once
let calls = 0
fn pick() { calls += 1; return 0 }
let arr = [100]
arr[pick()] += 1
print(arr[0])             # expect: 101
print(calls)              # expect: 1
# string concat with +=
let s = "be"
s += "ast"
print(s)                  # expect: beast
# on upvalues
fn make() {
    let n = 0
    return fn() { n += 7; return n }
}
let f = make()
f()
print(f())                # expect: 14
# on globals from inside a function
let g = 1
fn bump() { g += 1 }
bump()
bump()
print(g)                  # expect: 3
