fn(make()) {
  a := []
  add := () => { a.push(() => a.length())
    return a.length() }
  return add()
}
print(make())
