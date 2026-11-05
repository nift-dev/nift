// B4: an instance sent to a thread and back keeps its descriptor (and version): the
// run-time choice still finds C's own stringify (golden .vm).
struct(C) {
  a := 1
  private s := "SECRET-b4"
  fn(stringify()) { return "user" }
}
fn(echo(v)) { return v }
t := thread(echo, C())
back := t.join()
xs := [back, map()]
print("before")
for (v : xs) { print(v.stringify()) }
