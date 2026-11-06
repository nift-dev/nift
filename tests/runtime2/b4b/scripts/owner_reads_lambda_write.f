// B4b 26 (review of 4c9632b8, H-1, reviewer's H1): a function's local, written by a lambda whose write grows only in a later walk of the lambda; the function reads it after the call (was exit 70: a Static trim on a stale kind)
fn(t()) { return [1] }
fn(q()) { return t() }
fn(m()) {
  xs := [" s "]
  w := () => { xs = q()
  return 0 }
  w()
  return xs[0].trim()
}
print(m())
