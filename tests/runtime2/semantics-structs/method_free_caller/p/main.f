struct (P) { fn(m()) { return zz } }
p := P()
fn(g()) { zz := 4
return p.m() }
print(g())
