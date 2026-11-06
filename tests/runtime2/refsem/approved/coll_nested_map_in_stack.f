// D-NEXT-refsem #1 (review round 2): a loop over a map, set or stack yields elements the
// collection holds (in Phase B, aliases of every other holder): the binding is an
// element of an unknown holder. nested map in stack.
print("start")
st := stack()
m := map()
m.set("a", [1])
st.push(m)
for (mm : st) { for ((k, v) : mm) { v.push(9) } }
print(m.get("a").stringify())
