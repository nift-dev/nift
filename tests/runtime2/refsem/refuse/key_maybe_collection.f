// D-NEXT-refsem #6 (D6): a key that may be a collection (a map value read back).
print("start")
m := map()
m.set(1, map())
m.set(2, "x")
s := set()
s.add(m.get(1))
