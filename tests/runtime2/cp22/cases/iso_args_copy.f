x := [1, 2]
fn[async](m(a)) { a.push(3); return a.size() }
print(await m(x))
print(x.size())
o := {"k": 1}
fn[async](n(v)) { return v["k"] + 1 }
print(await n(o))
