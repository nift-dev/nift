fn(make(start)) {
  n := start
  return () => n++
}
c := make(5)
print(c())
d := {"c": c}
c = make(20)
print(d.c())
print(c())
fn(call(cb, x)) { return cb(x) }
print(call((v) => v + 1, 4))
