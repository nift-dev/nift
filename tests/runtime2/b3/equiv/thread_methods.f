// B3, owner ruling: instances crossing a thread are rendered (inside the
// thread and after) without running their own presentation methods.
struct(C) {
  n := 0
  private s := "SECRET-b3"
  fn(stringify()) { print("SIDE-EFFECT stringify"); n = n + 1; return "user" }
  fn(prettify()) { print("SIDE-EFFECT prettify"); n = n + 1; return "user" }
  fn(highlight()) { print("SIDE-EFFECT highlight"); n = n + 1; return "user" }
}
fn(w(v)) { return [v, [v].stringify()] }
c := C()
t := thread(w, c)
r := t.join()
print(r.stringify())
