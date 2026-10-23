fn(mk()) { return () => { return [1] } }
fn(t()) {
  h := mk()
  v := h()
  f := () => { print(v)
    return 0 }
  return f()
}
t()
