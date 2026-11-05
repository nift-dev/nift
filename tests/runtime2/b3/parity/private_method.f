// B3 (review L3): a private presentation method is not callable from outside
// the struct, so it is not a user method there: an outside `c.stringify()`,
// a container and a mixed receiver all render with the built-in (legacy
// parity), and the method never runs (no SIDE-EFFECT).
struct(C) {
  v := 0
  private s := "SECRET-b3"
  private fn(stringify()) { print("SIDE-EFFECT"); return "user" }
}
c := C()
print(c.stringify())
print([c].stringify())
xs := [C(), map()]
for (x : xs) { print(x.stringify()) }
