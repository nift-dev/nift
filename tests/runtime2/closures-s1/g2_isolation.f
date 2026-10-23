fn(counter(start)) {
  n := start
  return () => n++
}
a := counter(10)
b := counter(100)
print(a())
print(a())
print(b())
print(a())
fn(outer(base)) {
  n := base
  return () => { inner := n
    return () => inner++ }
}
o := outer(7)
x := o()
y := o()
print(x())
print(x())
print(y())
