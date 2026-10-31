m := map()
a := [m]
m.set(1, 1)
e := a[0]
print(e.size())
o := {"m": m}
m.set(2, 2)
h := o["m"]
print(h.size())
e.set(9, 9)
f := a[0]
print(f.size())
k := stack()
k.push(m)
m.set(3, 3)
top := k.top()
print(top.size())
