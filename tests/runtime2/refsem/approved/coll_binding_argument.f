// D-NEXT-refsem #1 (review round 2): a loop over a map, set or stack yields elements the
// collection holds (in Phase B, aliases of every other holder): the binding is an
// element of an unknown holder. binding argument.
print("start")
fn(grow(a)) { a.push(9); return 0 }
st := stack()
st.push([1])
for (x : st) { grow(x) }
print(st.top().stringify())
