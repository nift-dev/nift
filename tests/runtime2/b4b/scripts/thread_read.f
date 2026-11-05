// B4b C7: a container passed to a thread (copied at Spawn) and read there; the worker's
// stores and the sender's later stores never meet.
fn(w(a)) {
  return a[0].trim()
}
xs := [" a "]
t := thread(w, xs)
xs[0] = 5
print("before")
print(t.join())
