fn recurse(n) { return recurse(n + 1) }
recurse(0)
# expect runtime error: Stack overflow
# expect runtime error: more frames
