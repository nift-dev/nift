// D-NEXT-refsem #5 (D7): a global the task reads is copied with the arguments at creation.
g := map()
fn[async](w(a)) { return [g, a] }
f := w(g)
g.set(1, 1)
r := await f
x := r[0]
y := r[1]
print(same(x, y))
print(same(x, g))
print(x.size())
