fn(mk(v)) { return () => v }
fn(makeall(n)) {
  fs := []
  i := 0
  while (i < n) {
    fs.push(mk(i))
    i += 1
  }
  return fs.length()
}
print(makeall(1000))
