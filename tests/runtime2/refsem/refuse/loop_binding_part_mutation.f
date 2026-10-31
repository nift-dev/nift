// D-NEXT-refsem #1 (whole-program guard, review of ad9d842f): review j06: the loop binding is a part of the
// iterated array; mutating it changes the array only in Phase B.
print("start")
a := [[1], [2]]
for (r : a) { r.push(0); print(r.stringify()) }
print(a.stringify())
