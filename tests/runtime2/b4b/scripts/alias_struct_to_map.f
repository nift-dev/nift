// B4b: an element replaced through an alias (a map where a struct was), then a method,
// a field and a presentation call on the original binding.
struct(C) {
  a := 1
  fn(get(k)) { return a }
}
xs := [C()]
ys := xs
ys[0] = map()
print("before")
print(xs[0].get("k"))
print(xs[0].a)
print(xs[0].stringify())
