// B4b 32 (review of ce0a9a44, N-1): the reviewer's N1: a store through reverse()'s result reaches the receiver
xs := [" a ", " b "]
ys := xs.reverse()
ys[0] = 5
for (e : xs) { print(e.trim()) }
