m := map()
s := set()
k := stack()
i := 0
while (i < 5000) { m.set(i, [i]); s.add(i); k.push(map()); i += 1 }
n := m
n.set("x", 1)
m = map()
s = set()
k = stack()
n = map()
print(i)
