// B4b 26 (review of 4c9632b8, H-1, reviewer's H1c): the same with an object binding read by member
fn(t()) { return {"k": 1} }
fn(q()) { return t() }
fn(m()) {
  o := {"k": " s "}
  w := () => { o = q()
  return 0 }
  w()
  return o.k.trim()
}
print(m())
