// B4b 32 (review of ce0a9a44, N-1): the reviewer's N1c: reverse()'s result passed to a function that pushes
xs := [" a "]
fn(g()) { return 5 }
fn(h(a)) { a.push(g())
return 0 }
h(xs.reverse())
for (r : xs) { print(r.trim()) }
