// B4b 26 (review of 4c9632b8, H-1, reviewer's H1h): the owner is an imported module function.
fn(t()) { return [1] }
fn(q()) { return t() }
fn(m()) {
  xs := [" s "]
  w := () => { xs = q()
  return 0 }
  w()
  return xs[0].trim()
}
export(m)
