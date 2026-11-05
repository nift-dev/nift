// B4: a proven struct receiver calling a method the struct does not declare (a built-in
// name or any other): legacy's ordinary error "struct has no method: <name>" at the call,
// after the earlier output and before any argument is evaluated. B3 refused it before any
// effect (struct-no-method).
struct(C) {
  a := 1
  fn(get()) { return a }
}
fn(arg()) { print("arg"); return 1 }
c := C()
print("before")
for (i : [1]) { print(c.get()) }
print(c.size(arg()))
print("never")
