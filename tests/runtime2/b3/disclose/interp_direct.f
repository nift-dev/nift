// B3 non-disclosure: interpolation of an instance (B3: refused before running; B4: an ordinary
// run-time error, `struct instances cannot be rendered as text`)
struct(C) {
  a := 1
  private b := "SECRET-b3"
  fn(get()) { return b.length() }
}
c := C()
print("b")
print("v=$[c]")
