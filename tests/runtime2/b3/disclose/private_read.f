// B3 non-disclosure: reading a private field from outside (refused / legacy error)
struct(C) {
  a := 1
  private b := "SECRET-b3"
  fn(get()) { return b.length() }
}
c := C()
print("b")
print(c.b)
