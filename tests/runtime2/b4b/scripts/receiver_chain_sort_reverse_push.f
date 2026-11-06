// B4b 32 (review of ce0a9a44, N-1): a chained receiver-returning call, then a push (refused before any effect)
xs := [" a ", " b "]
ys := xs.sort().reverse()
ys.push(5)
for (e : xs) { print(e.trim()) }
