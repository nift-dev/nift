// U1 (found in the B4b F1 round; exit 70 on B4a 0df0df6c and e293105e): a lambda is walked
// before a later write grows the binding it captures (a function's parameter, assigned after the lambda that captures it);
// it is walked again and its trim is checked (the same error as legacy).
fn(t()) { return [1] }
fn(q()) { return t() }
fn(m(xs)) {
  g := () => { return xs[0].trim() }
  xs = q()
  print(g())
  return 0
}
m([" s "])
