def build(n):
    parts = []
    for i in range(n):
        parts.append("item-" + str(i))
    return len(parts)
total = 0
for _ in range(2000):
    total = build(1000)
print(total)
