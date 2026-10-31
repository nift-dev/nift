import("lib.f")
ts := []
fs := []
idx := 0
while (idx < 24) { ts.push(thread(work, idx)); fs.push(awork(idx)); idx += 1 }
idx = 0
while (idx < 24) {
  try { print(ts[idx].join()) } catch(e) { print(e.code + " " + e.message) }
  try { print(await fs[idx]) } catch(e) { print(e.code + " " + e.message) }
  idx += 1
}
pp := thread(sum_par, 16)
print(pp.join())
print(sum_par(8))
