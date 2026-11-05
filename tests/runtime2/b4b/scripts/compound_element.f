// B4b: a compound assignment on an element through an alias.
xs := ["a"]
ys := xs
ys[0] += "b"
print("before")
print(xs[0].length())
