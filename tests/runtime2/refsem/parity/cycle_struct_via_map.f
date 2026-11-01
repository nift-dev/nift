// D-NEXT-refsem #4: struct -> map -> struct (legacy follows collection values).
struct(N) { next := "" }
a := N()
m := map()
a.next = m
print("before")
m.set(1, a)
