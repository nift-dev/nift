// B3: the same through async tasks.
struct(C) {
  a := 1
  private b := "SECRET-b3"
}
fn[async](w(v)) { return [v] }
fn[async](make()) { return C() }
c := C()
f := w(c)
r := await f
print(r.stringify())
g := make()
x := await g
print(x.prettify())
