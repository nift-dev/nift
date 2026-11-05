// B4b C7: the worker changes its copy and returns it; the sender's container is unchanged.
fn(w(a)) {
  a[0] = 5
  return a
}
xs := [" a "]
t := thread(w, xs)
ys := t.join()
print("before")
print(xs[0].trim())
print(ys[0] + 1)
