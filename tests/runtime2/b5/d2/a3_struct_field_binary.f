// D2 (docs/runtime-2/checkpoints/combined-b3-b5.md section 27; owner rulings 2026-11-08 03:27/03:29) family A, a struct field on the left (already left to right; pinned)
struct(P) { v := 6 }
p := P()
g := () => { p.v = 20; return 4 }
print(p.v + g())
