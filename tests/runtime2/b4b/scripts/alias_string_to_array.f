// B4b: a string replaced by an array through an alias, then a string method on the original.
xs := [" a "]
ys := xs
ys[0] = [1, 2]
print("before")
print(xs[0].trim())
