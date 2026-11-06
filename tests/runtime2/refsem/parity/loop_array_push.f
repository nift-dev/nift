// D-NEXT-refsem #1 (whole-program guard, review of ad9d842f): the iterated array is mutated in the loop
// body (copy model: the loop sees a snapshot; Phase B: the live array).
print("start")
a := [1, 2, 3]
for (x : a) { a.push(x); print(x) }
print(a.stringify())
