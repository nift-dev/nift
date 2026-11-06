// D-NEXT-refsem #1 (D2 guard): a part read out, then mutated through a loop binding (below the top).
print("start")
a := [[1]]
x := a.first()
for (r : a) { r.push(2) }
print(x.stringify())
