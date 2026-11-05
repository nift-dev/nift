// B4 review H2 (field read): inside a struct method, a loop or catch binding named `this`
// shadows the instance in legacy; the VM's receiver analysis read `this` by its text and
// reached the enclosing instance. Refused before any effect (struct-this-binding).
struct(A) {
  a := 1
  fn(go(xs)) {
    for (this : xs) { print(this.a) }
    return 0
  }
}
print("before")
a := A()
print(a.go([{"a": 7}]))
