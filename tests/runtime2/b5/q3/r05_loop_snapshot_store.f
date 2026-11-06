// Q3 row 5 (§3 Iteration, D5): the loop reads its entry snapshot.
a := [1, 2]
for (v : a) { a[0] = 9; print(v) }
print(a.stringify())
