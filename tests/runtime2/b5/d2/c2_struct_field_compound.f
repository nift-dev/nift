// D2 (docs/runtime-2/checkpoints/combined-b3-b5.md section 27; owner rulings 2026-11-08 03:27/03:29) family C: `p.v += g()` reads the receiver and the field before the rhs (10; legacy cannot run it: struct has no method); a receiver-rebinding rhs stays refused (struct-assign, c3)
struct(P) { v := 6 }
p := P()
g := () => { p.v = 20; return 4 }
p.v += g()
print(p.v)
