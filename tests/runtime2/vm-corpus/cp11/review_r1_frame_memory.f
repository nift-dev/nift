fn(f(n, s)) {
  t := s + "xxxxxxxxxxxxxxxx"
  if (n == 0) { return 0 }
  return f(n - 1, t) + 1
}
print("start")
print(f(100005, "a"))
