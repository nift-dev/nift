// B4b: a reordering mutator through an alias (positions dropped per site).
struct(C) {
  a := 1
  fn(get(k)) { return a }
}
xs := [C(), map()]
ys := xs
ys.reverse()
print("before")
print(xs[0].get("k"))
