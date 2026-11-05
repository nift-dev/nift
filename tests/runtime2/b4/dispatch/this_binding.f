// B4 (B3 review N3): a loop binding named `this` outside any struct is an ordinary binding:
// its explicit stringify() renders through the built-in (B3 refused it, struct-private).
struct(C) {
  a := 1
  private fn(stringify()) { return "private" }
}
xs := [C()]
print("before")
for (this : xs) { print(this.stringify()) }
