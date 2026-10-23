fn(f(n, o)) {
  for ((k,v) : o) { print(v) }
  return n
}
print(f(7, {"a":1}))
