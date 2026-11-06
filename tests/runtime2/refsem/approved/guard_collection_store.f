// D-NEXT-refsem #1 (D2 guard): an array stored into a map, then mutated.
print("start")
a := [1]
m := map()
m.set(1, a)
a.push(2)
x := m.get(1)
print(x.stringify())
