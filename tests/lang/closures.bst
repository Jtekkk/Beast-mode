fn make_counter(start) {
    let n = start
    return fn(step) { n += step; return n }
}
let a = make_counter(0)
let b = make_counter(100)
print(a(1))               # expect: 1
print(a(1))               # expect: 2
print(b(5))               # expect: 105
print(a(1))               # expect: 3
# two closures share one captured variable
fn make_pair() {
    let shared = 0
    let inc = fn() { shared += 1; return shared }
    let read = fn() { return shared }
    return [inc, read]
}
let pair = make_pair()
let inc = pair[0]
let read = pair[1]
inc()
inc()
print(read())             # expect: 2
# for-loop variable is a fresh binding per iteration
let fns = []
for i in 0..3 {
    push(fns, fn() { return i })
}
print(fns[0]())           # expect: 0
print(fns[1]())           # expect: 1
print(fns[2]())           # expect: 2
# a let inside a while body is fresh per iteration too
let gs = []
let k = 0
while k < 3 {
    let v = k * 10
    push(gs, fn() { return v })
    k += 1
}
print(gs[2]())            # expect: 20
# ...but a variable declared outside the loop is shared
let hs = []
let m = 0
while m < 3 {
    push(hs, fn() { return m })
    m += 1
}
print(hs[0]())            # expect: 3
# deep nesting: capture across two levels
fn outer() {
    let x = "beast"
    fn middle() {
        fn inner() { return x }
        return inner
    }
    return middle()
}
print(outer()())          # expect: beast
# closure capturing a loop variable survives break
let esc = nil
for i in 0..10 {
    esc = fn() { return i }
    if i == 4 { break }
}
print(esc())              # expect: 4
