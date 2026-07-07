print("hello" + " " + "world")   # expect: hello world
print(len("beast"))              # expect: 5
print(len(""))                   # expect: 0
let s = "beast"
print(s[0])                      # expect: b
print(s[4])                      # expect: t
print("tab\tend")                # expect: tab	end
print("quote\"end")              # expect: quote"end
print("back\\slash")             # expect: back\slash
print(ord("A"))                  # expect: 65
print(chr(66))                   # expect: B
print(chr(ord("z")))             # expect: z
# interning: equal contents are the same value
print("be" + "ast" == "beast")   # expect: true
