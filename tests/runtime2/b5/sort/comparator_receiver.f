// B5 fix-up 2 (b5-refsem.md 12.4). Owner 2026-11-07 14:00: comparator sort returns the original
// receiver (approved difference from legacy). Copy-sort-commit (Q7): the comparator may run over
// temporary storage, but the result is committed once into the receiver and the receiver itself is
// returned. Returning the receiver is not the same as sorting in place: during the sort the receiver
// and its aliases keep the original order (aliased_reads.f), a failure commits nothing.
// Probe build only on this GCC 13.3 host (production refuses comparator sort there: D2).
xs := [3, 1, 2]
al := xs
holder := {"k": xs}
r := xs.sort((p, q) => p < q)
print(same(r, xs))
print(same(r, al))
print(al.stringify())
print(holder.k.stringify())
r.push(0)
print(xs.length())
print(al.stringify())
// sorted through an alias: the same array comes back
r2 := al.sort((p, q) => p > q)
print(same(r2, xs))
print(xs.stringify())
// a failing comparator: no result, the receiver, its aliases and its elements unchanged
a := [2]
b := [1]
ys := [a, b]
bl := ys
res := [42]
calls := 0
try {
  res = ys.sort((p, q) => { calls += 1; if (calls == 1) { throw error("stop") }; return p[0] < q[0] })
} catch (e) { print("caught " + e.message) }
print(res.stringify())
print(ys.stringify())
print(bl.stringify())
print(same(bl, ys))
y0 := ys[0]
print(same(y0, a))
// the same receiver then sorts normally, keeping its element identities
ok := ys.sort((p, q) => p[0] < q[0])
print(same(ok, bl))
f0 := bl[0]
print(same(f0, b))
b.push(5)
print(ok.stringify())
