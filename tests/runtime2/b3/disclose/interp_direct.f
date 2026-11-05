// B3 non-disclosure: interpolation of an instance (refused before running)
struct(C) {
  a := 1
  private b := "SECRET-b3"
  fn(get()) { return b.length() }
}
c := C()
print("b")
print("v=$[c]")
