fn(make()) {
  o := {"a": 1}
  f := () => o
  o = {"a": f}
  return 1
}
print(make())
