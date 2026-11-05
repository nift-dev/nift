// B3 (review N1): the module function mutates its array, then uses xs[0].
struct(C) {
  v := 7
  private s := "SECRET-b3"
  fn(stringify()) { print("SIDE-EFFECT"); return "user" }
  fn(get()) { print("SIDE-EFFECT get"); return v }
}
fn(show()) {
  xs := [C(), map()]
  xs.remove(0)
  d := xs[0]
  return d.stringify()
}
export(show)
