// D-NEXT-refsem #1 (review round 2): a loop over a map, set or stack yields elements the
// collection holds (in Phase B, aliases of every other holder): the binding is an
// element of an unknown holder. binding argument reader.
print("start")
g := [1]
fn(peek(a)) { g.push(2); return a.length() }
st := stack()
st.push(g)
for (x : st) { print(peek(x)) }
print(g.length())
