let m = { rex: 7, "nero": 5, 3: "three" }
print(m["rex"])           # expect: 7
print(m.rex)              # expect: 7
print(m["nero"])          # expect: 5
print(m[3])               # expect: three
print(len(m))             # expect: 3
# dot and bracket are the same field
m.fang = 9
print(m["fang"])          # expect: 9
m["fang"] = 10
print(m.fang)             # expect: 10
# missing keys read as nil
print(m.ghost)            # expect: nil
print(m["nope"])          # expect: nil
# get() with a default
print(get(m, "ghost", 0)) # expect: 0
print(get(m, "rex", 0))   # expect: 7
# del()
print(del(m, "rex"))      # expect: true
print(del(m, "rex"))      # expect: false
print(m.rex)              # expect: nil
print(len(m))             # expect: 3
# keys() + len
let total = 0
let ks = keys(m)
for i in 0..len(ks) { total += 1 }
print(total)              # expect: 3
# number keys: -0 and 0 are the same key
let z = {}
z[0] = "zero"
print(z[-0])              # expect: zero
# nested maps, multi-line literal
let cfg = {
    name: "beast",
    limits: { depth: 8 },
}
print(cfg.limits.depth)   # expect: 8
print(cfg.name)           # expect: beast
# maps are references
let n = m
n["new"] = 1
print(m["new"])           # expect: 1
# empty map prints as {}
print({})                 # expect: {}
# single-entry map printing (multi-entry order is unspecified)
print({ one: 1 })         # expect: {"one": 1}
