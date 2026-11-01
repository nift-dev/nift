// D-NEXT-refsem #1 (review round 2): a loop over a map, set or stack yields elements the
// collection holds (in Phase B, aliases of every other holder): the binding is an
// element of an unknown holder. review 2 repro t3.
print("start")
fn(mk(g)) { st := stack(); st.push(g); return st }
g := [1]
for (x : mk(g)) { x.push(9) }
print(g.stringify())
