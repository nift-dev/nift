fn[async](ticker(base, n)) { i := 0; s := 0; while (i < n) { sleep(2); s += i; i += 1 }; return base + s }
fn[async](collect()) { a := ticker(100, 3); b := ticker(200, 4); return (await a) * 1000 + (await b) }
print(await collect())
k := 0
while (k < 3) { print(k); k += 1 }
