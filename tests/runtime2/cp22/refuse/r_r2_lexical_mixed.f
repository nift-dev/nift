x := 1
fn[async](f()) { sleep(2); return x }
g := (x) => { return await f() }
fn(h(x)) { return await f() }
print(g(9))
print(h(9))
