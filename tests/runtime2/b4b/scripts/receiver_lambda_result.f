// B4b 32 (review of ce0a9a44, N-1): a lambda returns reverse() of its parameter
f := (a) => a.reverse()
xs := [" a ", " b "]
ys := f(xs)
ys.push(5)
for (e : xs) { print(e.trim()) }
