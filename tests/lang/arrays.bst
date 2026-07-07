let a = [1, 2, 3]
print(len(a))             # expect: 3
print(a[0])               # expect: 1
print(a[2])               # expect: 3
a[1] = 20
print(a[1])               # expect: 20
push(a, 4)
print(len(a))             # expect: 4
print(a[3])               # expect: 4
print(pop(a))             # expect: 4
print(len(a))             # expect: 3
# push returns the array, so calls chain
let b = push(push([], "x"), "y")
print(len(b))             # expect: 2
# arrays are references
let c = a
c[0] = 99
print(a[0])               # expect: 99
# nested + printing
let grid = [[1, 2], [3, 4]]
print(grid[1][0])         # expect: 3
print(grid)               # expect: [[1, 2], [3, 4]]
print([])                 # expect: []
print([1, "two", nil, true])    # expect: [1, "two", nil, true]
# trailing comma
let t = [1, 2, 3,]
print(len(t))             # expect: 3
# empty array is truthy
if [] { print("truthy") } # expect: truthy
# get() with default works on arrays
print(get(a, 0))          # expect: 99
print(get(a, 10, "none")) # expect: none
