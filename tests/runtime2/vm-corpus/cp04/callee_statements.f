fn(body()) {
  a := 1
  b := 1.0
  a += 2
  b += 2
  a++
  c := [a, b]
  c.push(9)
  s := "x"
  s += "y"
  print(a, b, c, s)
  return a
}
print(body())
r := body()
print(r)
