# top-level code can call functions defined later in the file
fn first() { return second() + 1 }
fn second() { return 41 }
print(first())            # expect: 42
# globals bind late: a function may use one declared after it
fn readCounter() { return counter }
let counter = 7
print(readCounter())      # expect: 7
# functions can assign globals
fn setCounter(v) { counter = v }
setCounter(99)
print(counter)            # expect: 99
