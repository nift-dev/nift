// B3 (review H1): the module declares C with its own stringify.
struct(C) {
  v := 7
  fn(stringify()) { print("SIDE-EFFECT explicit"); return "user" }
}
fn(make()) { return C() }
export(make)
