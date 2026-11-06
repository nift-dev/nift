// B4b 26 (review of 4c9632b8, H-1, reviewer's H1g): the owner a struct method
fn(t()) { return [1] }
fn(q()) { return t() }
struct(C) {
  a := 1
  fn(m()) {
    xs := [" s "]
    w := () => { xs = q()
    return 0 }
    w()
    return xs[0].trim()
  }
}
c := C()
print(c.m())
