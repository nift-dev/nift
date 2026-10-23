fn(make()) {
  g := () => null
  g = () => g
  return g
}
h := make()
print(h() == h)
