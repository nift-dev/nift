// H-A (docs/runtime-2/checkpoints/combined-b3-b5.md): set add: receiver first (legacy 0, 1).
s := set()
keep := s
fn(g()) { s = set(); return 4 }
s.add(g())
print(s.size())
print(keep.size())
