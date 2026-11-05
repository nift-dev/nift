// B3 non-disclosure: interpolation of a mixed loop element
struct(C) {
  a := 1
  private b := "SECRET-b3"
  fn(get()) { return b.length() }
}
xs := [1, C()]
print("b")
for (v : xs) { print("x$[v]") }
