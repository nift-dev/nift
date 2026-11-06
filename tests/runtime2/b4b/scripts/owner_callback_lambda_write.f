// B4b 26 (review of 4c9632b8, H-1, reviewer's H1f): the lambda invoked from a map callback
fn(t()) { return [1] }
fn(q()) { return t() }
fn(m()) {
  xs := [" s "]
  w := () => { xs = q()
  return 0 }
  r := [0].map((v) => w())
  return xs[0].trim()
}
print(m())
