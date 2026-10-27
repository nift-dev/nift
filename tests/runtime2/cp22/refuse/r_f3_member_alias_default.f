say := (n) => { print(n); return n }
o := {"push": say}
fn[async](t(io)) { m := io.push; return m(4) }
print(await t(o))
