fn(make()) {
  g := 0
  f := () => () => () => g
  g = f
  return 1
}
print(make())
