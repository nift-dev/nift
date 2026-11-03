// B2 review H1: a module function's object literal is fresh at every call.
fn(fresh()) {
  n := 0
  for ((k, v) : {"a": [1]}) {
    v.push(2)
    n = v.length()
  }
  return n
}
export(fresh)
