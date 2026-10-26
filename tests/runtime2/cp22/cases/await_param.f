fn[async](f(n)) { return n + 1 }
fn[async](g(p)) { return (await p) * 10 }
print(await g(f(1)))
k := async (q) => { return await q }
print(await k(f(5)))
