// B3: legacy's compact and pretty struct forms (b3-structs.md): the type
// name, public fields in declaration order, private fields skipped; pretty
// ends every field line with ",", `Name{}` when nothing is public.
struct(C) {
  a := 1
  private b := "SECRET-b3"
  c := "q\"t"
  d := 2.5
  e := true
  f := null
}
struct(D) {
  private x := "SECRET-b3"
}
struct(E) {
}
c := C()
print(c.stringify())
print(c.prettify())
print(c.highlight())
print(D().stringify())
print(D().prettify())
print(E().stringify())
print(E().prettify())
