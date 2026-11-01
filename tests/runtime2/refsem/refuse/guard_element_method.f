// D-NEXT-refsem #1 (D2 guard): an element returned by a method, then mutated.
print("start")
a := [[1]]
x := a.first()
x.push(2)
print(a.stringify())
