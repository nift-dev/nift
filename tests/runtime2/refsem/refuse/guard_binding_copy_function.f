// D-NEXT-refsem #1 (D2 guard): the D-040 witness inside a function (a global copied).
g := [1]
fn(f()) { x := g; x.push(2); return 0 }
print("start")
f()
print(g.stringify())
