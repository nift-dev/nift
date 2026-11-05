// P1 audit: nested assignment (the inner assignment's value stored again).
xs := [0]
ys := [0]
print(xs[0] = ys[0] = 5)
x := 0
y := 0
print(x = y = 7)
