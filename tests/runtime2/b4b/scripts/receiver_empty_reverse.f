// B4b 32 (review of ce0a9a44, N-1): reverse() of an empty literal, a push through the result
xs := []
ys := xs.reverse()
ys.push(5)
for (e : xs) { print(e.trim()) }
