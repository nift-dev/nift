fn(f(o)) {
  for ((k,v) : o) { print(v) }
  return 0
}
print(f({"a":[1,2],"b":3}))
