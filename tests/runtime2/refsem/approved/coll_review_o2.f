// D-NEXT-refsem #1 (review round 2): a loop over a map, set or stack yields elements the
// collection holds (in Phase B, aliases of every other holder): the binding is an
// element of an unknown holder. review 2 repro o2.
print("start")
m := map()
m.set("a", [3, 1, 2])
for ((k, v) : m) { v.sort() }
print(m.get("a").stringify())
