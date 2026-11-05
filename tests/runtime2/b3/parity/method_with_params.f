// B3, owner ruling: a struct method named like a presentation built-in but
// taking parameters (the nift-packages csv/url `stringify(rows)` shape) is not
// a presentation method: a zero-argument `.stringify()` renders the instance,
// on a proven instance and on a mixed receiver, and the method is still
// callable with its arguments.
struct(C) {
  v := 1
  private s := "SECRET-b3"
  fn(stringify(rows)) { return rows.length() }
}
c := C()
print(c.stringify())
print(c.stringify([1, 2, 3]))
xs := [C(), map()]
for (x : xs) { print(x.stringify()) }
