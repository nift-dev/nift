struct(C) {
  a := 1
  fn(get(k)) { return a }
}
xs := [C()]
fn(peek()) { return xs[0].get("k") }
export(xs)
export(peek)
