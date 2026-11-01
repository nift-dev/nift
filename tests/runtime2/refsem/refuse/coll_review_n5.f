// D-NEXT-refsem #1 (review round 2): a loop over a map, set or stack yields elements the
// collection holds (in Phase B, aliases of every other holder): the binding is an
// element of an unknown holder. review 2 repro n5.
print("start")
a := [1]
st := stack()
st.push(a)
st.push(a)
for (x : st) { x.push(9) }
for (y : st) { print(y.stringify()) }
