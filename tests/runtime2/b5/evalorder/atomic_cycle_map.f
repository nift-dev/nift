// H-A (docs/runtime-2/checkpoints/combined-b3-b5.md): the same for map set (receiver unchanged: size 0; the new map, 0).
m := map()
keep := m
g := () => { m = map(); return keep }
try { m.set("k", g()) } catch (e) { print("caught") }
print(m.size())
print(keep.size())
