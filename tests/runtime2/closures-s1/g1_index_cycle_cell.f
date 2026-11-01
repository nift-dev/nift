fn(make()) {
  a := [0]
  f := () => a.length()
  a[0] = f
  return 1
}
print(make())
