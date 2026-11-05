// B4b C7: a thread's argument is a copy; the worker's store is not seen by the sender.
fn(w(a)) {
  a[0] = 5
  return 0
}
xs := [" a "]
t := thread(w, xs)
t.join()
print("before")
print(xs[0].trim())
