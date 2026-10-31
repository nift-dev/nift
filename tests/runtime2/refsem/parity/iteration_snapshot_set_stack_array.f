// D5: sets, stacks and arrays iterate the membership at loop entry (legacy-equal).
s := set()
s.add(1)
s.add(2)
for (x : s) { s.add(x + 10); print(x) }
print(s.size())
st := stack()
st.push(1)
st.push(2)
for (x : st) { st.push(x * 10); print(x) }
print(st.size())
a := [1, 2, 3]
for (x : a) { a.push(x); print(x) }
print(a.stringify())
