// B4b: push through an alias, then the new position read through the original.
struct(C) { a := 1 }
xs := [C()]
ys := xs
ys.push(map())
print("before")
print(xs[1].a)
