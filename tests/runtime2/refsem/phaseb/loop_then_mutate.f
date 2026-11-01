// D-NEXT-refsem #1: admitted; loops over an array mutated only outside them.
a := [1, 2]
for (x : a) { print(x) }
a.push(3)
for (x : a) { print(x) }
