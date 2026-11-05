// B4 (review H1): the same through `this` inside a method: a method body counts as prepared
// code (CP12b, conservative: it may be called from a loop), so refused before any effect, as in B3.
struct(C) {
  a := 1
  fn(probe()) { return this.nosuch() }
}
c := C()
print("before")
print(c.probe())
