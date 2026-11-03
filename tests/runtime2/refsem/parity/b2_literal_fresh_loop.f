// B2 review H1: a JSON-spelled object literal is a new object at every
// evaluation (its nested array too): a push in one iteration is never seen by
// the next (legacy, the VM before B2 and B2 agree: 2 2 2).
for (i : range(0, 3)) {
  for ((k, v) : {"a": [1]}) {
    v.push(i)
    print(v.length())
  }
}
