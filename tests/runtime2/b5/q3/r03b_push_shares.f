// §3 Element store (Q3 row 3, the array form): the pushed array is shared, not copied.
x := [1]
ys := []
ys.push(x)
x.push(2)
print(ys.stringify())
