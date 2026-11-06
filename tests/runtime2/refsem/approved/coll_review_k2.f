// D-NEXT-refsem #1 (review round 2): a loop over a map, set or stack yields elements the
// collection holds (in Phase B, aliases of every other holder): the binding is an
// element of an unknown holder. review 2 repro k2.
print("start")
st := stack()
st.push([1])
s2 := deepcopy(st)
for (x : s2) { x.push(9) }
for (y : s2) { print(y.stringify()) }
