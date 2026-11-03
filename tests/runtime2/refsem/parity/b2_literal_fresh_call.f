// B2 review H1: the literal in a function is fresh at each call.
fn(f()) {
  for ((k, v) : {"a": [1]}) {
    v.push(5)
    print(v.stringify())
  }
  return 0
}
f()
f()
