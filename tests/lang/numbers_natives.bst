print(floor(3.7))         # expect: 3
print(floor(-3.2))        # expect: -4
print(sqrt(81))           # expect: 9
print(sqrt(2))            # expect: 1.4142135623731
print(num("42"))          # expect: 42
print(num("3.5"))         # expect: 3.5
print(num("1e2"))         # expect: 100
print(num("beast"))       # expect: nil
print(num(""))            # expect: nil
print(num(7))             # expect: 7
print(str(42))            # expect: 42
print(str(true) + str(nil))     # expect: truenil
print(str("already"))     # expect: already
print(typeof(1))          # expect: num
print(typeof("s"))        # expect: str
print(typeof(nil))        # expect: nil
print(typeof(true))       # expect: bool
print(typeof([]))         # expect: array
print(typeof({}))         # expect: map
print(typeof(print))      # expect: fn
fn f() { return 1 }
print(typeof(f))          # expect: fn
