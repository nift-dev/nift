// H-A (docs/runtime-2/checkpoints/combined-b3-b5.md): element store `b[i] = g()` where g changes i: the value is evaluated before the index in every context (legacy [0,5] each time).
i := 0
b := [0, 0]
fn(g()) { i = 1; return 5 }
b[i] = g()
print(b.stringify())
j := 0
c := [0, 0]
fn(h()) { j = 1; return 5 }
fn(t()) {
  c[j] = h()
  return 0
}
t()
print(c.stringify())
fn(u()) {
  k := 0
  d := [0, 0]
  v := () => { k = 1; return 5 }
  d[k] = v()
  print(d.stringify())
  return 0
}
u()
