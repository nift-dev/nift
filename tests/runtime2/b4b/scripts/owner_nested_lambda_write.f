// B4b 26: the general invariant, a nested lambda writes the local of the named function two
// lexical levels up; the function reads it after the call.
fn(t()) { return [1] }
fn(q()) { return t() }
fn(m()) {
  xs := [" s "]
  outer := () => {
    inner := () => { xs = q()
    return 0 }
    return inner()
  }
  outer()
  return xs[0].trim()
}
print(m())
