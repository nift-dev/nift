// D-NEXT-refsem #1 (D2 guard): an array read out of a map, then mutated.
print("start")
m := map()
m.set(1, [1])
x := m.get(1)
x.push(2)
y := m.get(1)
print(y.stringify())
