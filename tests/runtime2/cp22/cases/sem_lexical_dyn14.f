x := 1
fn[async](f()) { sleep(2); return x }
k := async (x) => { return await f() }
print(await k(9))
