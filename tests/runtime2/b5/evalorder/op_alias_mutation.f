// H-A (docs/runtime-2/checkpoints/combined-b3-b5.md): the argument mutates the receiver through an alias: one array, both see both pushes (legacy [1,9,4] twice).
b := [1]
keep := b
g := () => { keep.push(9); return 4 }
b.push(g())
print(b.stringify())
print(keep.stringify())
m := map()
mk := m
h := () => { mk.set("z", 9); return 4 }
m.set("k", h())
print(m.size())
print(mk.size())
