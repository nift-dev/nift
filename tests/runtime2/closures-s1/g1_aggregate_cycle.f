fn(make()) {
  g := () => null
  a := [1]
  a.push(() => g)
  g = () => a
  return g
}
h := make()
print(type(h()))
