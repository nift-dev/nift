// D-NEXT-refsem #1 (review round 2): a loop over a map, set or stack yields elements the
// collection holds (in Phase B, aliases of every other holder): the binding is an
// element of an unknown holder. review 2 repro m9.
print("start")
fn(f(st)) { for (x : st) { x.push(9) }; return 0 }
st := stack()
st.push([1])
f(st)
print(st.top().stringify())
