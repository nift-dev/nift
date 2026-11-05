// B3 non-disclosure: string concatenation with an instance
struct(C) {
  a := 1
  private b := "SECRET-b3"
  fn(get()) { return b.length() }
}
c := C()
print("b")
print("v=" + c)
