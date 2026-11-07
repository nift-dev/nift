// M-3 (combined-b3-b5.md 8; owner 2026-11-07 18:00 D5): a deferred precision limitation. Under the entry
// snapshot nothing can vanish, yet this legitimate program is refused (for-capture-vanishing) before any
// effect, as on 17f7964c. Legacy prints [[1,7],[2]]. Kept so a future admission is a visible change.
rows := [[1], [2]]
gs := []
for (r : rows) {
  for (c : r) { gs.push(() => r) }
}
g0 := gs[0]
x := g0()
x.push(7)
print(rows.stringify())
