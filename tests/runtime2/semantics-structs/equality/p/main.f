struct (P) { x := 1 }
a := P()
b := P()
c := a
print(a == b)
print(a == c)
b.x = 1
print(a != b)
print(a == {"x": 1})
