// D-NEXT-refsem #1: a loop binding is the element itself.
m1 := map()
m2 := map()
for (x : [m1, m2]) { x.set(1, 1) }
print(m1.size() + m2.size())
