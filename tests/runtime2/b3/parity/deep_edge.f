// B3: the depth limit exactly: 128 nested instances render, 129 do not.
struct(N) {
  next := ""
  private s := "SECRET-b3"
  fn(set(x)) { next = x }
}
fn(mk()) { return N() }
head := mk()
cur := head
for (i : range(0, 127)) {
  nx := mk()
  cur.set(nx)
  cur = nx
}
print("built")
print(head.stringify().length())
last := mk()
cur.set(last)
print(head.stringify().length())
