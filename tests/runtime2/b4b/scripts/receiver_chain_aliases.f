// B4b 32 (review of ce0a9a44, N-1): reverse() then sort() through the first result: all three name the receiver
xs := [" a ", " b "]
ys := xs.reverse()
zs := ys.sort()
zs[0] = 5
for (e : xs) { print(e.trim()) }
