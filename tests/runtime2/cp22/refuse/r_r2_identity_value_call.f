nap := (ms) => { return ms }
fn[async](spin(n, f)) { i := 0; while (i < n) { f(1); i += 1 }; return i }
print(await spin(20000, nap))
