// B4b 32 (review of ce0a9a44, N-1): an element array sorted, a push through the result
xs := [[" b ", " a "]]
inner := xs[0]
ys := inner.sort()
ys.push(5)
for (e : xs[0]) { print(e.trim()) }
