x := 1
fn[async](f()) { sleep(2); return x }
g := (x) => { return f() }
p := g(9)
print(await p)
