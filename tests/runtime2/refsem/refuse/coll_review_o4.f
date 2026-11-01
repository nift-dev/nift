// D-NEXT-refsem #1 (review round 2): a loop over a map, set or stack yields elements the
// collection holds (in Phase B, aliases of every other holder): the binding is an
// element of an unknown holder. review 2 repro o4.
print("start")
// map passed to a function (reference), the function loops and mutates values
fn(bump(m)) { for ((k, v) : m) { v[0] = v[0] + 1 }; return 0 }
m := map()
m.set("a", [1])
bump(m)
bump(m)
print(m.get("a").stringify())
