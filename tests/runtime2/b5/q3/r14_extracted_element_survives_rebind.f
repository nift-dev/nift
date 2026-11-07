// §3 Extraction (owner 2026-11-07 15:25, compatibility.md row): `x := a[0]` binds the element
// itself, not a location in `a`; reassigning `a` leaves `x` valid and unchanged.
// Hand-derived: 1 (scalar: legacy agrees), [1], 1 (legacy: "reference target no longer exists: y"
// and "...: z.k"), and 1 for a nested element after `c` is rebound (legacy reads the location
// through the new `c`: 5).
a := [1]
x := a[0]
a = []
print(x)
b := [[1]]
y := b[0]
b = []
print(y.stringify())
o := [{"k": 1}]
z := o[0]
o = []
print(z.k)
c := [[1]]
w := c[0]
c = [[5]]
print(w[0])
