// B3: an instance reached through several references (aliases, container
// elements) renders the same everywhere, at each occurrence, with privacy.
struct(C) {
  a := 1
  private b := "SECRET-b3"
}
c := C()
d := c
d.a = 5
xs := [c, d]
print(c.stringify())
print(xs.stringify())
fn(show(v)) { return v.stringify() }
print(show(d))
print(show(xs))
