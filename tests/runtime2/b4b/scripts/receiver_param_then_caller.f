// B4b 32 (review of ce0a9a44, N-1): a function pushes through a comparator sort of its parameter; the caller reads
fn(g(a)) {
  ys := a.sort((p, q) => true)
  ys.push(5)
  return 0
}
xs := [" a "]
g(xs)
for (e : xs) { print(e.trim()) }
