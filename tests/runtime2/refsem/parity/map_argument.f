// D-NEXT-refsem #1: a synchronous argument is the same map.
fn(fill(t)) { t.set("k", 1); return t.size() }
m := map()
print(fill(m))
print(m.size())
print(m.get("k"))
