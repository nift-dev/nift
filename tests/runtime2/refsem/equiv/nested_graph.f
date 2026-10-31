// D-NEXT-refsem #5 (D7): a nested graph keeps its internal sharing in the copy.
fn[async](w(o)) { return o }
inner := map()
outer := map()
outer.set("a", inner)
outer.set("b", inner)
f := w(outer)
inner.set(9, 9)
r := await f
p := r.get("a")
q := r.get("b")
print(same(p, q))
print(same(p, inner))
print(p.size())
