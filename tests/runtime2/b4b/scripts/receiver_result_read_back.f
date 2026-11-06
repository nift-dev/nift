// B4b 32 (review of ce0a9a44, N-1): a store through the receiver, read through reverse()'s result
xs := [" a ", " b "]
ys := xs.reverse()
xs[0] = 5
for (e : ys) { print(e.trim()) }
