// D-NEXT-refsem #4 (D1): a cycle through an array is refused (legacy accepts it: a new limitation).
m := map()
try { m.set(1, [m]) } catch (e) { print(e.message) }
print(m.size())
