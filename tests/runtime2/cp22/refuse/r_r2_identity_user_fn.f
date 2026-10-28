fn(nap(ms)) { return ms }
fn[async](spin(n)) { i := 0; while (i < n) { nap(1); i += 1 }; return i }
print(await spin(20000))
