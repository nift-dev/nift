// B4b: an element replaced inside a closure, observed after the call.
xs := [" a "]
g := () => {
  xs[0] = 5
  return 0
}
g()
print("before")
print(xs[0].trim())
