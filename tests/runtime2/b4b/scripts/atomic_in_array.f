// B4b: an atomic swapped into an array through an alias, then a string method.
xs := [" a "]
ys := xs
ys[0] = atomic<int>(5)
print("before")
print(xs[0].trim())
