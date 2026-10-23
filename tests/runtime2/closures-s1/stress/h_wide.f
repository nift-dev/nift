fn(makeall(n)) {
  a := 1
  b := 2
  c := 3
  d := 4
  e := 5
  f := 6
  g := 7
  h := 8
  j := 9
  k := 10
  l := 11
  m := 12
  o := 13
  p := 14
  q := 15
  r := 16
  s := 17
  t := 18
  u := 19
  w := 20
  fs := []
  i := 0
  while (i < n) {
    fs.push(() => a + b + c + d + e + f + g + h + j + k + l + m + o + p + q + r + s + t + u + w)
    i += 1
  }
  return fs.length()
}
print(makeall(200000))
