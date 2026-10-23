fn(k()) { return 1 }
fn(make()) {
  g := 0
  f := () => { g = k
    return 1 }
  return f()
}
print(make())
