// B2 review H1 (case table: a literal built inside a closure): fresh per
// evaluation, so the closure's second call does not see the first's push.
g := () => {
  for ((k, v) : {"a": [1]}) {
    v.push(2)
    print(v.length())
  }
  return 0
}
g()
g()
