heavy := (n) => { i := 0; while (i < n) { i += 1 }; return i }
o := {"push": heavy}
fn[async](t(obj)) { return obj.push(1000) }
print(await t(o))
