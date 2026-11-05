// P1 audit: element stores with every literal kind as a value, and register values.
xs := [0]
print(xs[0] = "s")
print(xs[0] = 2.5)
print(xs[0] = -1)
print(xs[0] = [1, 2])
print(xs[0] = {"a": 1})
v := "r"
print(xs[0] = v)
