// D-NEXT-refsem #1 (review round 2): a loop over a map, set or stack yields elements the
// collection holds (in Phase B, aliases of every other holder): the binding is an
// element of an unknown holder. review 2 repro m6.
print("start")
m := map()
m.set("k", [1])
for ((k, v) : m) { v[0] = 5 }
print(m.get("k").stringify())
