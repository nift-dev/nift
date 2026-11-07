// B5 fix-up 2 (b5-refsem.md 12.4). Owner 2026-11-07 14:00: reverse() and plain sort() return the
// original receiver (an APPROVED intentional difference from legacy, which reverses or sorts in place
// and returns a copy). Checked here: the returned value is the receiver itself and every alias of it
// (identity), aliases observe the successful mutation, the return value introduces no fresh identity
// (a mutation through it reaches the receiver, every alias and every container holding it), and the
// elements keep their identity. Production build (comparator sort: tests/runtime2/b5/sort).
xs := [3, 1, 2]
al := xs
holder := {"k": xs}
r := xs.reverse()
print(xs.stringify())
print(al.stringify())
print(same(r, xs))
print(same(r, al))
r.push(7)
print(xs.length())
print(al.stringify())
print(holder.k.stringify())
// through an alias receiver: the result is still the one array
r2 := al.reverse()
print(same(r2, xs))
print(xs.stringify())
// plain sort
ys := [5, 4, 6]
bl := ys
s := ys.sort()
print(ys.stringify())
print(bl.stringify())
print(same(s, ys))
print(same(s, bl))
s.push(0)
print(bl.stringify())
s2 := bl.sort()
print(same(s2, ys))
print(ys.stringify())
// element identity is kept by both
p := [1]
q := [2]
zs := [p, q]
z1 := zs.reverse()
f0 := zs[0]
print(same(f0, q))
q.push(9)
print(z1.stringify())
words := ["b", "a"]
w := words.sort()
print(same(w, words))
print(words.stringify())
