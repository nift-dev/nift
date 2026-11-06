// Q3 row 6 (§3 Iteration, D5): every entry element is visited after a pop.
b := [1, 2, 3]
for (v : b) { b.pop(); print(v) }
print(b.stringify())
