fn(make()) {
  f := () => null
  g := () => null
  f = () => g
  g = () => f
  return {"f": f, "g": g}
}
p := make()
print(p.f() == p.g())
