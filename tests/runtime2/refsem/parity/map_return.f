// D-NEXT-refsem #1: a returned map is the same object.
fn(id(t)) { return t }
m := map()
q := id(m)
q.set(1, 1)
print(m.size())
print(same(m, q))
