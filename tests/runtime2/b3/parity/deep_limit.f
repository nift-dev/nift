// B3: beyond legacy's depth limit (129 nested levels) rendering stops with
// legacy's error, never loops.
struct(N) {
  next := ""
  private s := "SECRET-b3"
  fn(set(x)) { next = x }
}
fn(mk()) { return N() }
head := mk()
cur := head
for (i : range(0, 140)) {
  nx := mk()
  cur.set(nx)
  cur = nx
}
print("built")
print(head.stringify().length())
