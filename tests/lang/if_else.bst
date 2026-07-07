let x = 10
if x > 5 {
    print("big")          # expect: big
}
if x > 100 {
    print("huge")
} else {
    print("not huge")     # expect: not huge
}
if x < 0 {
    print("neg")
} else if x == 10 {
    print("ten")          # expect: ten
} else {
    print("other")
}
if x > 1 { if x > 2 { print("nested") } }    # expect: nested
