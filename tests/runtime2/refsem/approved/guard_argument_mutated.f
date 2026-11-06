// D-NEXT-refsem #1 (D2 guard): f(a) where f mutates its parameter (Phase B: the caller sees it).
fn(f(p)) { p.push(9); return 0 }
print("start")
a := [1]
f(a)
print(a.stringify())
