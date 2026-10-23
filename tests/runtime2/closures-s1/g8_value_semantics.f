fn(make(a)) {
  return () => { a.push(2)
    return a.stringify() }
}
a := [1]
f := make(a)
print(f())
print(a.stringify())
fn(copy_out()) {
  b := [1]
  get := () => b
  x := get()
  x[0] = 2
  return b.stringify()
}
print(copy_out())
