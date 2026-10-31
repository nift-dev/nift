// D-NEXT-refsem (D2 guard): building and mutating an exclusive array still runs.
a := []
for (i : [1, 2, 3]) { a.push(i) }
a.push(4)
a[0] = 9
print(a.stringify())
