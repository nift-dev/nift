// B3, owner ruling: an explicit stringify() on a receiver that may be an
// instance of a struct declaring its own stringify (a loop over mixed
// values) would have to choose at run time: refused before any effect
// (presentation-struct-method, B4 dynamic dispatch).
struct(C) {
  n := 0
  private s := "SECRET-b3"
  fn(stringify()) { print("SIDE-EFFECT stringify"); n = n + 1; return "user" }
  fn(prettify()) { print("SIDE-EFFECT prettify"); n = n + 1; return "user" }
  fn(highlight()) { print("SIDE-EFFECT highlight"); n = n + 1; return "user" }
}
print("before")
xs := [C(), map()]
for (x : xs) { print(x.stringify()) }
