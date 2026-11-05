// B4b C2: a mutating method on a receiver that may be an array.
xs := [" a "]
v := xs
if (xs.size() > 5) { v = "q" }
v.push(4)
print("before")
print(xs[1].trim())
