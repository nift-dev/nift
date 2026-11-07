// H-A (docs/runtime-2/checkpoints/combined-b3-b5.md): map set: receiver first (legacy 0, 1).
m := map()
keep := m
g := () => { m = map(); return 4 }
m.set("k", g())
print(m.size())
print(keep.size())
