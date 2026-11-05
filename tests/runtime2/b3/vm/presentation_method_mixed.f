// B3, owner ruling; B4: an explicit stringify()/prettify() on a receiver that may be an
// instance of a struct declaring its own method chooses at run time (StructIs): the
// struct's own method for its instances, the built-in otherwise. B3 refused it
// (presentation-struct-method). Legacy ignores the method: an approved difference (owner,
// 2026-11-05: an explicit call runs the struct's own presentation method).
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
