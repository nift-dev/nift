// Q3 row 11 (§3 same): identity, never an error.
x := [1]
y := x
z := [1]
print(same(x, y))
print(same(x, z))
print(x == z)
