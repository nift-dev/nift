struct (Q) { v := 1 }
struct (P) { q := Q() }
p := P()
p.q.v = 7
print(p.q.v)
r := P()
print(r.q.v)
