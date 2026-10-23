fn(make()) {
  a := [0]
  f := () => a
  a[0] = f
  return 1
}
print(make())
