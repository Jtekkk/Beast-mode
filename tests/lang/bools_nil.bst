# Only nil and false are falsey.
print(not nil)            # expect: true
print(not false)          # expect: true
print(not true)           # expect: false
print(not 0)              # expect: false
print(not "")             # expect: false
print(not [])             # expect: false
print(not {})             # expect: false
print(not not 42)         # expect: true
# not binds looser than comparison
print(not 1 == 2)         # expect: true
