// D-NEXT-refsem #6 (D5, re-affirming D-063 #7): a map loop iterates the entries at loop entry.
m := map()
m.set(1, "a")
m.set(2, "b")
for ((k, v) : m) { m.set(k + 10, v); m.set(2, "z"); print(k); print(v) }
print(m.size())
