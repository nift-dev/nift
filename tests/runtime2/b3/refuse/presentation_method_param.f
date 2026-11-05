// B3, owner ruling: the same through a parameter called with an instance and
// with an array (prettify).
struct(C) {
  n := 0
  private s := "SECRET-b3"
  fn(stringify()) { print("SIDE-EFFECT stringify"); n = n + 1; return "user" }
  fn(prettify()) { print("SIDE-EFFECT prettify"); n = n + 1; return "user" }
  fn(highlight()) { print("SIDE-EFFECT highlight"); n = n + 1; return "user" }
}
fn(show(v)) { return v.prettify() }
print("before")
print(show([1]))
print(show(C()))
