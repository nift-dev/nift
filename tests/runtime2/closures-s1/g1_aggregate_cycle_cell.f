fn(make()) {
  a := [1]
  f := () => a.length()
  a.push(f)
  return 1
}
print(make())
