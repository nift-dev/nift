fn(f()) { return zz }
fn(g()) { zz := 5; return f() }
print(g())
