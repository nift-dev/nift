fn(f(arr)) {
  for (x : arr) { print(x["k"]) }
  return 0
}
print(f([{"k":1},{"k":2}]))
