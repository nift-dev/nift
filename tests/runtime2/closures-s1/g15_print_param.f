fn(mk()) { return () => { return [1] } }
fn(show(a)) { print(a)
  return 0 }
h := mk()
show(h())
