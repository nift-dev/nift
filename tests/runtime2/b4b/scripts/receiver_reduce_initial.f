// B4b 32 (review of ce0a9a44, N-1): control: reduce returns its initial argument (an alias of the argument, modelled before)
xs := [" a ", " b "]
ys := xs.reduce((acc, e) => acc, xs)
ys[0] = 5
for (e : xs) { print(e.trim()) }
