// D-NEXT-refsem #1: admitted; a loop over a collection that mutates nothing it yields
// (the elements are only read): Phase B prints the same.
s := set()
s.add(1)
s.add(2)
for (x : s) { print(x) }
