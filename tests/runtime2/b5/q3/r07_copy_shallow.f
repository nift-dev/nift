// Q3 row 7 (§3 copy): shallow; the inner array is shared.
orig := [[1]]
d := copy(orig)
inner := d[0]
inner.push(2)
print(orig.stringify())
