fn(f(o)) {
  for ((k,v) : o) { print(v) }
  for ((k,v) : o) { print(k) }
  return 0
}
print(f({"a":1}))
