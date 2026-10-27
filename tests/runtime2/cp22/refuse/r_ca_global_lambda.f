work := (n) => { i := 0; while (i < n) { i += 1 }; return i }
fn[async](go(n)) { return work(n) }
print(await go(1000))
