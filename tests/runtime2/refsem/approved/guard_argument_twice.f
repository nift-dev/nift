// D-NEXT-refsem #1 (D2 guard): one array passed twice to a mutating function.
fn(f(p, q)) { p.push(9); return q.size() }
print("start")
a := [1]
print(f(a, a))
