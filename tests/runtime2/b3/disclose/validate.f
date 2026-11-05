// B3 non-disclosure: validate() of a value holding an instance
struct(C) {
  a := 1
  private b := "SECRET-b3"
  fn(get()) { return b.length() }
}
c := C()
print("b")
v := [c]
print(validate({}, v))
