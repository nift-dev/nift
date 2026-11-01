// D-NEXT-refsem #1 (D2 guard): a mutated copy of an array (a former D-040 witness).
print("start")
a := [1]
b := a
b.push(2)
print(a.stringify())
