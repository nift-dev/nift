// B3 (review N1), B4: after `pop` the instance that was at xs[0] may have moved, so
// xs[0] is not a proven struct receiver. B3 refused the call before any effect
// (presentation-struct-method:stringify); B4 chooses at run time (StructIs): the receiver
// is the value now in that position, so the built-in renders it (legacy parity).
struct(C) {
  v := 7
  private s := "SECRET-b3"
  fn(stringify()) { print("SIDE-EFFECT"); return "user" }
  fn(get()) { print("SIDE-EFFECT get"); return v }
}
print("before")
xs := [C(), map()]
xs.pop()
xs.pop()
xs.push(map())
d := xs[0]
print(d.stringify())
