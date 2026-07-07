print(str([1, [2, 3]]))       # expect: [1, [2, 3]]
print(str({}) + str([]))      # expect: {}[]
print("n=" + str(nil))        # expect: n=nil
print("" + str(1.5))          # expect: 1.5
fn f() { return 0 }
print(str(f))                 # expect: <fn f>
print(str(print))             # expect: <native fn print>
# strings nested in arrays are quoted; bare strings are not
print(str(["a", "b"]))        # expect: ["a", "b"]
print(str("a"))               # expect: a
print(-0)                     # expect: 0
