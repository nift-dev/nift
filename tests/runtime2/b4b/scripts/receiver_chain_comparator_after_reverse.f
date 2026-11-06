// B4b 32 (review of ce0a9a44, N-1): a comparator sort of reverse()'s result, then a push
xs := [" a ", " b "]
ys := xs.reverse()
zs := ys.sort((p, q) => true)
zs.push(5)
for (e : xs) { print(e.trim()) }
