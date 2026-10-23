fn(g()) {
  a := [1,2,3]
  for (x : a) { a = [9]; print("s"); if (false) { print(x) } }
  return 0
}
print(g())
