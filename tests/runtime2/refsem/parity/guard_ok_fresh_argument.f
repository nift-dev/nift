// D-NEXT-refsem (D2 guard): a fresh argument to a mutating parameter.
fn(f(p)) { p.push(9); return p.size() }
print(f([1, 2]))
print(f([]))
