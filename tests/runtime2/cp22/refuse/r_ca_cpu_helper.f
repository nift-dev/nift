fn(spin(n)) { i := 0; while(i < n) { i += 1 }; return i }
fn[async](work(n)) { return spin(n) }
print(await work(1000))
