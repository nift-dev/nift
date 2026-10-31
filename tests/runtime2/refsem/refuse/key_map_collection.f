// D-NEXT-refsem #6 (D6): a mutable collection as a map key is refused before any effect.
print("start")
m := map()
k := map()
m.set(k, 1)
