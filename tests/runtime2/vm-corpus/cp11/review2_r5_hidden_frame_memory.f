fn(mk(n)) {
  s := "B" + n
  k := 0
  while(k < 16) { s += s; k += 1 }
  return [[s]]
}
fn(f(n)) {
  if(n == 0) { return 0 }
  w := mk(n)
  return f(n - 1) + w.length()
}
print(f(60000))
