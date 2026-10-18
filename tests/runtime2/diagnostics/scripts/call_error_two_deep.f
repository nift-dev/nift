fn(inner(o)) {
  return o.missing
}
fn(outer(o)) {
  return inner(o)
}
print(outer({"a": 1}))
