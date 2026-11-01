// D-NEXT-refsem #1: a closure captures the binding; the value it returns is the map.
m := map()
f := () => m
q := f()
q.set(1, 1)
print(m.size())
g := () => m.size()
m.set(2, 2)
print(g())
