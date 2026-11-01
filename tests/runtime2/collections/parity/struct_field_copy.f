struct(Box) { v := map() }
b := Box()
w := b.v
w.set(1, 1)
b.v = w
w.set(2, 2)
x := b.v
print(x.size())
print(w.size())
