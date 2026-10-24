fn(inner(n)) {
  if (n == 0) { throw error("bottom") }
  return inner(n - 1)
}
fn(mid()) {
  a := "mid-local"
  try { inner(5) } catch(e) { print(a + " " + e.message) }
  return a
}
print(mid())
