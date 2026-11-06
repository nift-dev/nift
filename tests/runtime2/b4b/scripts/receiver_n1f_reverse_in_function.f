// B4b 32 (review of ce0a9a44, N-1): the reviewer's N1f: reverse() of a parameter inside a function
fn(f(a)) { b := a.reverse()
b[0] = 5
return 0 }
xs := [" a ", " b "]
f(xs)
for (e : xs) { print(e.trim()) }
