// B3 (review N1): after `splice2` the instance that was at xs[0] may have moved, so
// xs[0] is no longer a proven struct receiver: the present use is refused before any
// effect (presentation-struct-method:stringify). Before the fix the VM ran C's own method on a map.
struct(C) {
  v := 7
  private s := "SECRET-b3"
  fn(stringify()) { print("SIDE-EFFECT"); return "user" }
  fn(get()) { print("SIDE-EFFECT get"); return v }
}
print("before")
xs := [C(), map()]
xs.splice(0, 1)
d := xs[0]
print(d.stringify())
