fn(makeall(n)) {
  fs := []
  i := 0
  while (i < n) {
    fs.push((x) => x)
    i += 1
  }
  return fs.length()
}
print(makeall(200000))
