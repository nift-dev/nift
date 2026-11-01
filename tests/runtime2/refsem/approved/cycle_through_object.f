// D-NEXT-refsem #4 (D1): a cycle through an object is refused (legacy accepts it).
m := map()
try { m.set(1, {"k": m}) } catch (e) { print(e.message) }
print(m.size())
