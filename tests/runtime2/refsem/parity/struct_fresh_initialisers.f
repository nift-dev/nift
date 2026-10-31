// D-NEXT-refsem #1 (D4): a map()/set()/stack() field initialiser is fresh per instance.
struct(Box) { m := map(); s := set(); k := stack() }
a := Box()
b := Box()
w := a.m
w.set(1, 1)
x := b.m
print(x.size())
y := a.m
print(y.size())
u := a.s
u.add(1)
z := b.s
print(z.size())
t := a.k
t.push(1)
r := b.k
print(r.size())
print(same(w, x))
