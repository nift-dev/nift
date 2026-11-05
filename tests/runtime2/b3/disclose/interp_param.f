// B3 non-disclosure: interpolation of an instance through a parameter (an ordinary error)
struct(C) {
  a := 1
  private b := "SECRET-b3"
  fn(get()) { return b.length() }
}
fn(show(v)) { return "i$[v]" }
print("b")
print(show(1))
print(show(C()))
