// B4 (b4-dispatch.md 6, owner): interpolating a struct instance is an ordinary run-time
// error after the earlier output (legacy prints its internal tag, not reproduced); no
// private field and no user method is involved (golden .vm).
struct(C) {
  a := 1
  private s := "SECRET-b4"
  fn(stringify()) { print("SIDE-EFFECT"); return "user" }
}
c := C()
print("before")
print("v=$[c]")
print("never")
