// D-NEXT-refsem #1 (D2 guard): copy() is shallow: the copy mutated below its top level.
print("start")
a := [[1]]
b := copy(a)
for (r : b) { r.push(9) }
print(a.stringify())
