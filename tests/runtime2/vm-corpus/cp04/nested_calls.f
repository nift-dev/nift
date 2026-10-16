fn(a(x)) { return b(x) + 1 }
fn(b(x)) { return x * 2 }
v := a(a(1))
print(v)
w := [a(1), a(2)]
print(w)
