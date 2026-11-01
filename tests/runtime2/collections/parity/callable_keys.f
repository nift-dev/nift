f := (x) => x
g := (x) => x + 1
m := map()
m.set(f, "fn")
print(m.get(f))
print(m.contains(g))
s := set()
s.add(f)
print(s.contains(f))
print(s.contains(g))
