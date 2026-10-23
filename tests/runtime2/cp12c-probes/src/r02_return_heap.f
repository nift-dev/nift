fn(f(o)) {
  for ((k,v) : o) { if (k == "a") { return 1 } }
  return 0
}
o := {"a":[1,2],"b":[3]}
print(f(o))
