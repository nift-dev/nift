// B5 (owner Q7 ruling 2026-11-07, copy-sort-commit; deferred B2 L1): every read of the receiver
// through an alias while the comparator runs sees the original order; the result is the receiver.
xs := [5, 3, 4, 1, 2]
ys := xs
seen := []
r := xs.sort((p, q) => { seen.push(ys.stringify()); return p < q })
print(xs.stringify())
print(same(r, xs))
print(same(ys, xs))
bad := 0
for (s : seen) { if (s != "[5,3,4,1,2]") { bad += 1 } }
print(bad)
