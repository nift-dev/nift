fn(make()) {
  a := 1
  b := 10
  inner := () => b
  outer := () => { inner()
    return a }
  return outer
}
f := make()
print(f())
