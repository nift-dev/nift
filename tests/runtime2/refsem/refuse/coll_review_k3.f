// D-NEXT-refsem #1 (review round 2): a loop over a map, set or stack yields elements the
// collection holds (in Phase B, aliases of every other holder): the binding is an
// element of an unknown holder. review 2 repro k3.
print("start")
// loop binding over a stack captured by a closure that mutates it later
st := stack()
st.push([1])
fs := []
for (x : st) { x.push(5) }
print(st.top().stringify())
st2 := copy(st)
for (x : st2) { x.push(6) }
print(st.top().stringify())
