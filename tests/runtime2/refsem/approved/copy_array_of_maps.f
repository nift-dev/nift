// D-NEXT-refsem #3 (D3): an array is a value until Phase B; copy() shares the maps it holds.
m := map()
a := [m]
b := copy(a)
x := b[0]
x.set(1, 1)
print(m.size())
c := deepcopy(a)
y := c[0]
y.set(2, 2)
print(m.size())
