print(1 < 2)              # expect: true
print(2 <= 2)             # expect: true
print(3 > 4)              # expect: false
print(4 >= 4)             # expect: true
print(1 == 1)             # expect: true
print(1 == 2)             # expect: false
print(1 != 2)             # expect: true
print("a" == "a")         # expect: true
print("a" == "b")         # expect: false
print(1 == "1")           # expect: false
print(nil == nil)         # expect: true
print(nil == false)       # expect: false
print(true == true)       # expect: true
let a = [1]
let b = [1]
print(a == b)             # expect: false
print(a == a)             # expect: true
