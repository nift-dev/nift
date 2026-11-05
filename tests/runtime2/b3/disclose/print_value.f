// B3 non-disclosure: print of an instance (legacy and VM: not directly renderable)
struct(C) {
  a := 1
  private b := "SECRET-b3"
  fn(get()) { return b.length() }
}
c := C()
print("b")
print(c)
