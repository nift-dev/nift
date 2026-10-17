fn(g(x)) {
  print("g")
  return x
}
fn(f(p)) {
  return (true ? p : 0)
}
c := 0
while(c < 1) {
  c += 1
  f(g(1))
}
