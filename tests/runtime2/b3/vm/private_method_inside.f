// B3 (review L3, owner ruling): inside the struct's own methods `this` reaches
// its private stringify: the explicit call runs it (legacy ignores user
// presentation methods: the approved difference of vm/user_method.f).
struct(C) {
  v := 0
  private s := "SECRET-b3"
  private fn(stringify()) { print("SIDE-EFFECT inside"); return "user" }
  fn(show()) { return this.stringify() }
}
c := C()
print(c.show())
print(c.stringify())
