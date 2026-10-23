g := (n, o) => {
  for ((k,v) : o) { print(v) }
  return n }
print(g(7, {"a":1}))
