// B4 review H2 (private method): inside a struct method, a loop or catch binding named `this`
// shadows the instance in legacy; the VM's receiver analysis read `this` by its text and
// reached the enclosing instance. Refused before any effect (struct-this-binding).
struct(A) {
  a := 1
  private fn(stringify()) { print("SIDE A a=" + a); return "userA" }
  fn(go(xs)) {
    for (this : xs) { print(this.stringify()) }
    return 0
  }
}
struct(B) {
  private q := "SECRET"
  b := 2
}
a := A()
print("before")
print(a.go([B()]))
