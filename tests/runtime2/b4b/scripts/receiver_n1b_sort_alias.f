// B4b 32 (review of ce0a9a44, N-1): the reviewer's N1b: the same through sort()
xs := [" a ", " b "]
ys := xs.sort()
ys[0] = 5
for (e : xs) { print(e.trim()) }
