struct(Box) { v := map() }
b := Box()
d := Box()
w := b.v
w.set(1, 1)
x := d.v
print(x.size())
print(type(b.v))
