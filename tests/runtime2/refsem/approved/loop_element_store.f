// D-NEXT-refsem #1 (whole-program guard, review of ad9d842f): review j01: an element store into the iterated array.
print("start")
a := [1, 2]
for (x : a) { a[1] = 9; print(x) }
print(a.stringify())
