fn(f(p)) { p.push(9) }
fn(g(q)) { f(q) }
o := {"k": [1]}
a := [[1]]
g(o.k)
f(a[0])
print(o.k.length())
print(a[0].length())
