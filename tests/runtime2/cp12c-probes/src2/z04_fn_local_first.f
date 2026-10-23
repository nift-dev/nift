fn(f(o)) {
  s := 0
  for ((k,v) : o) { s += v }
  return s
}
print(f({"a":1,"b":2}))
