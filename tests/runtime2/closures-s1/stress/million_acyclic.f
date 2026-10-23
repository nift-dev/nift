fn(burn()) {
  i := 0
  while (i < 1000000) {
    x := i
    g := () => x
    g = () => null
    i += 1
  }
  return 1
}
print(burn())
