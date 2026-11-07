// D2 (docs/runtime-2/checkpoints/combined-b3-b5.md section 27; owner rulings 2026-11-08 03:27/03:29) family A, an implicit field on the left in a method (pinned; legacy 10 20)
struct(P) {
  v := 6
  fn(sum(h)) { return v + h() }
}
p := P()
g := () => { p.v = 20; return 4 }
print(p.sum(g))
print(p.v)
