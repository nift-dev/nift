// B4b 32 (review of ce0a9a44, N-1): a binding reassigned to reverse()'s result
xs := [" a ", " b "]
ys := [" c "]
ys = xs.reverse()
ys[0] = 5
for (e : xs) { print(e.trim()) }
