// D-NEXT-refsem #1: assignment shares a map (legacy-equal).
m := map()
b := m
b.set(1, "x")
print(m.get(1))
print(same(m, b))
c := map()
c = m
c.set(2, "y")
print(m.size())
