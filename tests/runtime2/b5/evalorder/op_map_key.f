// H-A (docs/runtime-2/checkpoints/combined-b3-b5.md): map set whose key rebinds the receiver; map remove (legacy 0 1, then 1 0).
m := map()
keep := m
g := () => { m = map(); return "k" }
m.set(g(), 1)
print(m.size())
print(keep.size())
m2 := map()
m2.set("k", 1)
k2 := m2
h := () => { m2 = map(); m2.set("k", 2); return "k" }
m2.remove(h())
print(m2.size())
print(k2.size())
