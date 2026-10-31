// D-NEXT-refsem #1 (D4): containers hold the instance.
struct(P) { v := 0 }
p := P()
a := [p]
p.v = 4
q := a[0]
print(q.v)
m := map()
m.set("k", p)
r := m.get("k")
r.v = 6
print(p.v)
print(same(p, r))
st := stack()
st.push(p)
t := st.top()
t.v = 8
print(p.v)
