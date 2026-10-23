fn(burn()) {
  state := 0
  keep := () => state++
  i := 0
  while (i < 1000000) {
    x := i
    g := () => x
    g = () => null
    i += 1
  }
  return keep
}
f := burn()
print(f())
print(f())
