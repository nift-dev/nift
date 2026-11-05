// B3 non-disclosure: a field type error names the field, never a value
struct(C) {
  a := 1
  private b := "SECRET-b3"
  fn(get()) { return b.length() }
}
c := C()
print("b")
c.a = "SECRET-b3-2"
print(c.stringify())
