struct(C) { a := 1
 fn(trim()) { return "own" } }
xs := [" a "]
ys := xs
ys[0] = C()
print(xs[0].trim())
