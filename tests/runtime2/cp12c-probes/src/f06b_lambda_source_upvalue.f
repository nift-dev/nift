fn(runit()) {
  a := [1,2,3]
  g := () => {
    for (x : a) { a = [9]; print(x) }
    return 0 }
  return g()
}
print(runit())
