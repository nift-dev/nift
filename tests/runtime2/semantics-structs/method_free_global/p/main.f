k := 5
struct (P) { fn(m()) { return k } }
p := P()
print(p.m())
