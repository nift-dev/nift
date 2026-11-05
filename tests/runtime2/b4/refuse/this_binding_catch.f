// B4 review H2 (catch binding): inside a struct method, a loop or catch binding named `this`
// shadows the instance in legacy; the VM's receiver analysis read `this` by its text and
// reached the enclosing instance. Refused before any effect (struct-this-binding).
struct(A) {
  a := 1
  fn(peek()) { return a }
  fn(go()) {
    try { throw error("x") } catch (this) { print(this.peek()) }
    return 0
  }
}
print("before")
a := A()
print(a.go())
