// D-NEXT-refsem #1 (D4): struct instances are references.
struct(P) { v := 0 }
a := P()
b := a
b.v = 5
print(a.v)
print(same(a, b))
