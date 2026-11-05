// P1: the same inside a function, a string constant, as a returned value.
fn(g()) {
  xs := [1]
  return (xs[0] = "k")
}
print(g())
