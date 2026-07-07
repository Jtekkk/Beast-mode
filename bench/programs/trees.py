def make(depth):
    if depth == 0: return [None, None]
    return [make(depth - 1), make(depth - 1)]
def check(node):
    if node[0] is None: return 1
    return 1 + check(node[0]) + check(node[1])
total = 0
for _ in range(80):
    tree = make(12)
    total += check(tree)
print(total)
