// D-NEXT-refsem #4: a closure over a top-level map reads it by name (no heap cycle): stored (legacy-equal).
m := map()
f := () => m.size()
m.set("f", f)
print(m.size())
g := m.get("f")
print(g())
