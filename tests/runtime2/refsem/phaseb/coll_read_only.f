// D-NEXT-refsem #1: admitted; a loop over a collection that mutates nothing it yields
// (the elements are only read): Phase B prints the same.
st := stack()
st.push([1, 2])
for (x : st) { print(x.length()) }
