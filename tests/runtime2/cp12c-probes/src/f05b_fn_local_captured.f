fn(runit()) {
  a := [1,2,3]
  s := () => { a = [9]
    return 0 }
  for (x : a) { s(); print(x) }
  return 0
}
print(runit())
