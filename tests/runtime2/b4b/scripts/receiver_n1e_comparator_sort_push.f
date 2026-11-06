// B4b 32 (review of ce0a9a44, N-1): the reviewer's N1e: a push through a comparator sort's result
xs := [[" a "]]
ys := xs.sort((p, q) => true)
ys.push([5])
for (r : xs) { print(r[0].trim()) }
