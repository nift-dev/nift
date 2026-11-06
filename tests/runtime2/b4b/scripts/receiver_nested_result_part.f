// B4b 32 (review of ce0a9a44, N-1): a part of a comparator sort's result (the element sites, shared before too)
xs := [[" a "], [" b "]]
ys := xs.sort((p, q) => true)
part := ys[0]
part[0] = 5
for (r : xs) { print(r[0].trim()) }
