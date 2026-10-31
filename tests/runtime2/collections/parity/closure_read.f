m := map(); m.set(1, 2)
f := () => m.size()
print(f())
m.set(3, 4)
print(f())
fn(mk()) { k := stack(); k.push(1); return () => k.size() }
g := mk()
print(g())
