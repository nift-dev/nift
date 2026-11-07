// M-3 (combined-b3-b5.md 8; owner D5): deferred precision limitation, refused before any effect.
// Legacy prints [[1,9,8],[2,9]].
src := [[1], [2]]
fs := []
for (x : src) {
  fs.push(() => x)
  x.push(9)
}
f0 := fs[0]
r := f0()
r.push(8)
print(src.stringify())
