fn(t()) {
  f := () => y
  y := 4
  return f()
}
print(t())
