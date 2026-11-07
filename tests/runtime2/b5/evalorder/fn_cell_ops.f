// H-A (docs/runtime-2/checkpoints/combined-b3-b5.md): a function's captured locals: every operation (legacy [7], [4,1]; [7,8], [2]; 0, 1).
fn(t()) {
  b := [1]
  keep := b
  g := () => { b = [7]; return 4 }
  b.insert(0, g())
  n := b.push(g())
  print(b.stringify())
  print(keep.stringify())
  c := [1, 2]
  kc := c
  h := () => { c = [7, 8]; return 0 }
  c.remove(h())
  print(c.stringify())
  print(kc.stringify())
  m := map()
  km := m
  w := () => { m = map(); return 4 }
  m.set("k", w())
  print(m.size())
  print(km.size())
  return 0
}
t()
