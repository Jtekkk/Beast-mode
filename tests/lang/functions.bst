fn add(a, b) {
    return a + b
}
print(add(2, 3))          # expect: 5
# bare return returns nil
fn noop() { return }
print(noop())             # expect: nil
# falling off the end returns nil
fn nothing() { }
print(nothing())          # expect: nil
# first-class: assign, pass, return
let f = add
print(f(4, 5))            # expect: 9
fn apply(g, x) { return g(x, x) }
print(apply(add, 21))     # expect: 42
# anonymous functions
let double = fn(x) { return x * 2 }
print(double(8))          # expect: 16
# recursion
fn fact(n) {
    if n <= 1 { return 1 }
    return n * fact(n - 1)
}
print(fact(10))           # expect: 3628800
# mutual recursion through late-bound globals
fn isEven(n) {
    if n == 0 { return true }
    return isOdd(n - 1)
}
fn isOdd(n) {
    if n == 0 { return false }
    return isEven(n - 1)
}
print(isEven(10))         # expect: true
print(isOdd(7))           # expect: true
# local functions
{
    fn inner(x) { return x + 1 }
    print(inner(1))       # expect: 2
}
# local recursion sees its own slot
{
    fn count(n) {
        if n == 0 { return 0 }
        return 1 + count(n - 1)
    }
    print(count(4))       # expect: 4
}
