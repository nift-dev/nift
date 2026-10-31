// D-NEXT-refsem #1 (D2 guard): a copy through ?? that is mutated.
print("start")
a := [1]
b := a ?? [2]
b.push(2)
print(a.stringify())
