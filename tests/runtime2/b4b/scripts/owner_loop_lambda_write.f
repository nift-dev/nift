// B4b 26 (review of 4c9632b8, H-1, reviewer's H1d): the same, read in a loop after the first iteration ran
fn(t()) { return [1] }
fn(q()) { return t() }
fn(m()) {
  xs := [" s "]
  w := () => { xs = q()
  return 0 }
  i := 0
  while(i < 2) {
    print(xs[0].trim())
    w()
    i += 1
  }
  return 0
}
m()
