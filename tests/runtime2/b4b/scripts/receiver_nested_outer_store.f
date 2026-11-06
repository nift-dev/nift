// B4b 32 (review of ce0a9a44, N-1): an element of reverse()'s result replaced by an int: the outer receiver's element
xs := [[" a "], [" b "]]
ys := xs.reverse()
ys[1] = 5
for (r : xs) { print(r[0].trim()) }
