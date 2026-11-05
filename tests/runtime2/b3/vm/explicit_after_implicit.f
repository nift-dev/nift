// B3, owner ruling: the explicit call runs the user method (its side effect
// and result), while implicit renderings before and after it do not; the
// counter shows exactly one call. Legacy ignores the method: an approved
// difference (vm/user_method.f).
struct(C) {
  n := 0
  private s := "SECRET-b3"
  fn(stringify()) { print("SIDE-EFFECT stringify"); n = n + 1; return "user" }
  fn(prettify()) { print("SIDE-EFFECT prettify"); n = n + 1; return "user" }
  fn(highlight()) { print("SIDE-EFFECT highlight"); n = n + 1; return "user" }
}
c := C()
print([c].stringify())
print(c.stringify())
print([c].stringify())
