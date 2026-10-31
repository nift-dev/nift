a := map()
f := () => a
x := f()
x.set(1, 1)
print(a.size())
g := (m) => m
y := g(a)
y.set(2, 2)
print(a.size())
print(y.size())
