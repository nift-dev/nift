// D-NEXT-refsem #1 (review round 2): a loop over a map, set or stack yields elements the
// collection holds (in Phase B, aliases of every other holder): the binding is an
// element of an unknown holder. closure capture.
print("start")
st := stack()
st.push([1])
fs := []
for (x : st) { fs.push(() => x.push(9)) }
for (f : fs) { f() }
print(st.top().stringify())
