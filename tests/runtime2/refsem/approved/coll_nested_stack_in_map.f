// D-NEXT-refsem #1 (review round 2): a loop over a map, set or stack yields elements the
// collection holds (in Phase B, aliases of every other holder): the binding is an
// element of an unknown holder. nested stack in map.
print("start")
m := map()
st := stack()
st.push([1])
m.set("s", st)
for ((k, s) : m) { for (x : s) { x.push(9) } }
print(st.top().stringify())
