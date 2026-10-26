fn[async](f(n)) { return n * 3 }
fn(helper(p)) { return await p }
a := f(1)
print(helper(a))
h := f(5)
fn(g()) { return await h }
print(g())
