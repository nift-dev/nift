// B4b: an element replaced inside a function, observed by the caller.
fn(f(a)) {
  a[0] = 5
  return 0
}
xs := [" a "]
f(xs)
print("before")
print(xs[0].trim())
