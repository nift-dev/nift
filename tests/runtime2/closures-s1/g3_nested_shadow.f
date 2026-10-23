fn(make(base)) {
  x := base
  return () => { y := 2
    return () => { z := 3
      return () => x + y + z } }
}
f := make(10)
g := f()
h := g()
print(h())
fn(shadow()) {
  x := 1
  return (x) => () => x
}
s := shadow()
t := s(5)
print(t())
