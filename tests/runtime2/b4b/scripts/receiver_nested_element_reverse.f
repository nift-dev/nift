// B4b 32 (review of ce0a9a44, N-1): an element array reversed: the store reaches the outer array's element
xs := [[" a ", " b "], [" c "]]
inner := xs[0]
ys := inner.reverse()
ys[0] = 5
for (e : xs[0]) { print(e.trim()) }
