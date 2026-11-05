// B4: the same through `this` inside a method (reported at the call: D-009#5, golden .vm).
struct(C) {
  a := 1
  fn(probe()) { return this.nosuch() }
}
c := C()
print("before")
print(c.probe())
