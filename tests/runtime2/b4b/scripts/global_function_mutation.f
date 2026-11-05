// B4b: a function replacing a global array's element; main observes it.
xs := [" a "]
fn(f()) {
  xs[0] = 5
  return 0
}
f()
print("before")
print(xs[0].trim())
