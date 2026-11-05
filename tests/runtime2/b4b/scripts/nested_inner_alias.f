// B4b: a nested container mutated through an inner alias.
xs := [[" a "]]
inner := xs[0]
inner[0] = 7
print("before")
print(xs[0][0].trim())
