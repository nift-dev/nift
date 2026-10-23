f := () => { g := () => z
  z := 9
  return g() }
print(f())
