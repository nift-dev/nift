// H-A (docs/runtime-2/checkpoints/combined-b3-b5.md): element store and map set into containers nothing else holds.
e := [42]
b := [e, 2]
keep := b
g := () => { b = [7]; keep = [8]; return e }
b[1] = g()
print(b.stringify())
print(keep.stringify())
m := map()
mk := m
h := () => { m = map(); mk = map(); return e }
m.set("k", h())
print(m.size())
print(e.stringify())
