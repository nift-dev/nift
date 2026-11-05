// B3, owner ruling (2026-11-05): an explicit call of a struct's own
// stringify/prettify/highlight method runs that method (the VM: 7, 8, 9).
// Legacy ignores the method and prints the descriptor form (C{v:0}, ...): an
// approved difference from legacy. Implicit presentation never runs it
// (implicit_methods.f).
struct(C) {
  v := 0
  private w := "SECRET-b3"
  fn(stringify()) { return 7 }
  fn(prettify()) { return 8 }
  fn(highlight()) { return 9 }
}
c := C()
print(c.stringify())
print(c.prettify())
print(c.highlight())
