// B4b 32 (review of ce0a9a44, N-1): a function returns reverse() of its parameter; the caller stores through it
fn(r(a)) { return a.reverse() }
xs := [" a ", " b "]
ys := r(xs)
ys[0] = 5
for (e : xs) { print(e.trim()) }
