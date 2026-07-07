fn make(depth) {
    if depth == 0 { return [nil, nil] }
    return [make(depth - 1), make(depth - 1)]
}
fn check(node) {
    if node[0] == nil { return 1 }
    return 1 + check(node[0]) + check(node[1])
}
let total = 0
for i in 0..80 {
    let tree = make(12)
    total += check(tree)
}
print(total)
