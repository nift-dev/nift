// H-A (docs/runtime-2/checkpoints/combined-b3-b5.md): the same in a lambda body, map and index read too.
b := [1, 2]
keep := b
fn(g()) { b = [7, 8]; return 0 }
m := map()
mk := m
fn(h()) { m = map(); return 4 }
t := () => {
  print(b[g()])
  b.push(g())
  m.set("k", h())
  return 0
}
t()
print(b.stringify())
print(keep.stringify())
print(m.size())
print(mk.size())
