o := {"push": 0}
fn(walk(n)) { if (n == 0) { return 0 }; return walk(n - 1) + 1 }
o = {"push": walk}
fn[async](t(io)) { return io.push(5) }
print(await t(o))
