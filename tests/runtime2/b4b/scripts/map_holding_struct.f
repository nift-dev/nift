// B4b: a container stored in a map, mutated after a lookup (the map's contents per site).
struct(C) { a := 1 }
m := map()
xs := [C()]
m.set("k", xs)
ys := m.get("k")
ys[0] = "s"
print("before")
print(xs[0].a)
