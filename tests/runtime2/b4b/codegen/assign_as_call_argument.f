// P1 audit: an assignment passed directly as a call argument (a user function and a native).
xs := [0, 0]
fn(show(v)) { return v + 1 }
print(show(xs[0] = 5))
print([xs[1] = 6].size())
x := 0
print(show(x = 2))
