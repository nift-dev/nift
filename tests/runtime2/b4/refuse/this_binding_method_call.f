// B4 review H2 (method call): inside a struct method, a loop or catch binding named `this`
// shadows the instance in legacy; the VM's receiver analysis read `this` by its text and
// reached the enclosing instance. Refused before any effect (struct-this-binding).
struct(A) {
  a := 1
  fn(peek()) { print("A.peek runs"); return a }
  fn(go(xs)) {
    for (this : xs) { print(this.peek()) }
    return 0
  }
}
struct(B) { b := 2 }
a := A()
print("before")
print(a.go([B()]))
