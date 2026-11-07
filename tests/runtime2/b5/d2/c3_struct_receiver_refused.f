// D2 (docs/runtime-2/checkpoints/combined-b3-b5.md section 27; owner rulings 2026-11-08 03:27/03:29) family C, out of scope: a compound on a field whose rhs rebinds the receiver stays refused before any effect (struct-assign)
struct(P) { v := 6 }
p := P()
g := () => { p.v = 20; return 4 }
p.v += g()
print(p.v)
q := P()
keep := q
h := () => { q = P(); return 4 }
q.v += h()
print(q.v)
print(keep.v)
