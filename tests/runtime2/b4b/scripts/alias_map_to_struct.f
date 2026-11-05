// B4b: the reverse: a struct where a map was, stored through the original, read through the alias.
struct(C) { a := 1 }
xs := [map()]
ys := xs
xs[0] = C()
print("before")
print(ys[0].get("k"))
print(ys[0].size())
