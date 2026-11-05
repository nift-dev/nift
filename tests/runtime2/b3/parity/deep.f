// B3: a deep chain of instances renders up to legacy's depth limit (beyond
// it rendering stops with legacy's error, never loops).
struct(N) {
  next := ""
  private s := "SECRET-b3"
  fn(set(x)) { next = x }
}
fn(chain(k)) {
  n := N()
  if (k > 0) { n.set(chain(k - 1)) }
  return n
}
c := chain(60)
print("built")
print(c.stringify().length())
