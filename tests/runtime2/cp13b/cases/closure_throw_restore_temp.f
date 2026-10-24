fn(make()) {
  msg := "ok"
  g := () => {
    try {
      f := () => { throw error("x") }
      f()
    } catch(e) {
      return "caught " + e.message + " " + msg
    }
  }
  return g
}
h := make()
print(h())
