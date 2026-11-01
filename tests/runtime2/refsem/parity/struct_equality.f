// D-NEXT-refsem #2 (D4): struct == is identity (legacy).
struct(P) { v := 0 }
a := P()
b := P()
c := a
print(a == b)
print(a == c)
d := copy(a)
print(a == d)
print(same(a, d))
