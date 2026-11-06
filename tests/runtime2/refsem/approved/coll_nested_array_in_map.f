// D-NEXT-refsem #1 (review round 2): a loop over a map, set or stack yields elements the
// collection holds (in Phase B, aliases of every other holder): the binding is an
// element of an unknown holder. nested array in map.
print("start")
m := map()
m.set("a", [[1]])
for ((k, v) : m) { for (r : v) { r.push(9) } }
print(m.get("a").stringify())
