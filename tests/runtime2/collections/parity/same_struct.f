fn(mk()) { m := map(); m.set("a", 1); return m }
x := mk()
y := mk()
print(x == y)
print(same(x, y))
