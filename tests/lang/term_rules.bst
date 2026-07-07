# semicolons allow several statements on one line
let a = 1; let b = 2; print(a + b)   # expect: 3
# a closing brace ends a statement without a terminator
fn f(n) { let m = n * 2; return m }
print(f(21))              # expect: 42
# newlines inside parens and brackets never terminate
let long = (1 +
    2 +
    3)
print(long)               # expect: 6
let arr = [
    1,
    2,
    3
]
print(len(arr))           # expect: 3
print(f(
    5
))                        # expect: 10
# a trailing operator continues the line
let sum = 1 +
    2
print(sum)                # expect: 3
# blank lines and stray semicolons are fine
;;
print("ok")               # expect: ok
# bare return on its own line returns nil (Go-style)
fn g() {
    return
}
print(g())                # expect: nil
