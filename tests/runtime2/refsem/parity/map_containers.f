// D-NEXT-refsem #1: containers hold references.
m := map()
a := [m]
x := a[0]
x.set(1, 1)
print(m.size())
o := {"k": m}
y := o["k"]
y.set(2, 2)
print(m.size())
outer := map()
outer.set("i", m)
z := outer.get("i")
z.set(3, 3)
print(m.size())
st := stack()
st.push(m)
t := st.top()
t.set(4, 4)
print(m.size())
print(same(m, t))
