// B4b 32 (review of ce0a9a44, N-1): sort() of an element read from a native's result (a derived site), a push through the result
o := {"k": [" a "]}
xs := o.values()[0]
ys := xs.sort()
ys.push(5)
for (e : xs) { print(e.trim()) }
