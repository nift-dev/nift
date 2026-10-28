x := 1
fn[async](f()) { sleep(2); return x }
fn(g(x)) { return await f() }
print(g(9))
