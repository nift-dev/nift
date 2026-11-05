// B3: copy and deepcopy keep the descriptor (privacy and form).
struct(C) {
  a := 1
  private b := "SECRET-b3"
}
c := C()
d := copy(c)
e := deepcopy([c])
print(d.stringify())
print(e.stringify())
print(same(c, d))
