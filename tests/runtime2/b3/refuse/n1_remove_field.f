// B3 (review N1): after `remove` the instance that was at xs[0] may have moved, so
// xs[0] is no longer a proven struct receiver: the field use is refused before any
// effect (type:member). Before the fix the VM ran C's own method on a map.
struct(C) {
  v := 7
  private s := "SECRET-b3"
  fn(stringify()) { print("SIDE-EFFECT"); return "user" }
  fn(get()) { print("SIDE-EFFECT get"); return v }
}
print("before")
xs := [C(), map()]
xs.remove(0)
d := xs[0]
print(d.v)
