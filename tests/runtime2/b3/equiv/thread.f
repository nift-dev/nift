// B3: instances crossing a thread keep their descriptor (name, order and
// privacy), both ways, including instances constructed inside the thread.
struct(C) {
  a := 1
  private b := "SECRET-b3"
  c := 3
}
fn(echo(v)) { return [v, v.stringify()] }
fn(make()) { return C() }
c := C()
t := thread(echo, c)
r := t.join()
print(r.stringify())
u := thread(make)
print(u.join().stringify())
