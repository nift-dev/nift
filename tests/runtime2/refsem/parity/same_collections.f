// D-NEXT-refsem #2: same() is identity (legacy-equal); == stays structural.
a := map()
b := map()
c := a
print(same(a, c))
print(same(a, b))
print(a == b)
s := set()
t := s
print(same(s, t))
k := stack()
print(same(k, k))
