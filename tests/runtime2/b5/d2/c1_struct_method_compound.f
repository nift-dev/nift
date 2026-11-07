// D2 (docs/runtime-2/checkpoints/combined-b3-b5.md section 27; owner rulings 2026-11-08 03:27/03:29) family C: an implicit field compound in a method reads the field before the rhs; legacy-equal (10)
struct(P) {
  v := 6
  fn(bump(h)) { v += h(); return v }
}
p := P()
g := () => { p.v = 20; return 4 }
print(p.bump(g))
