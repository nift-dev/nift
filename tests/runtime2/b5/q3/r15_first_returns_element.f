// §3 Extraction (owner 2026-11-07 15:25, compatibility.md row): `first()` returns the element
// itself: a mutation through the result is seen through the array (legacy: a copy).
// Hand-derived: [[1,2,9],[3]] and true (legacy: [[1,2],[3]] and false).
a := [[1, 2], [3]]
head := a.first()
head.push(9)
print(a.stringify())
e := a[0]
print(same(head, e))
