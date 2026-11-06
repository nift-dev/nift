// D-NEXT-refsem #1 (review round 2): a loop over a map, set or stack yields elements the
// collection holds (in Phase B, aliases of every other holder): the binding is an
// element of an unknown holder. closure capture reader.
print("start")
g := [1]
st := stack()
st.push(g)
h := () => 0
for (x : st) { h = () => x.length() }
g.push(2)
print(h())
