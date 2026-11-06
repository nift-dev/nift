// D-NEXT-refsem #1 (review round 2): a loop over a map, set or stack yields elements the
// collection holds (in Phase B, aliases of every other holder): the binding is an
// element of an unknown holder. review 2 repro m4.
print("start")
m := map()
m.set("k", [1])
n := m
for ((k, v) : n) { v.push(9); print(v.stringify()) }
print(m.get("k").stringify())
