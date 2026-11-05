// B4: a proven struct receiver calling a method the struct does not declare, outside
// prepared code, by a name that is not a string method's: legacy's ordinary error "struct
// has no method: <name>" at the call, after the earlier output and before any argument is
// evaluated. B3 refused it before any effect (struct-no-method); string method names are
// still refused (review H1: refuse/struct_no_method_*).
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
