// B4b 32 (review of ce0a9a44, N-1): a push on a call result (legacy cannot: refused before any effect)
fn(r(a)) { return a.sort() }
xs := [" a ", " b "]
r(xs).push(5)
for (e : xs) { print(e.trim()) }
