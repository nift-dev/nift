// B4b 32 (review of ce0a9a44, N-1): a chained receiver-returning call (legacy cannot chain: refused before any effect)
xs := [" a ", " b "]
ys := xs.reverse().sort()
ys[0] = 5
for (e : xs) { print(e.trim()) }
