id := (n) => { return n }
fn[async](spin(fs)) { i := 0; for (sleep : fs) { sleep(1); while (i < 300) { sleep(1); i += 1 } }; return i }
print(await spin([id]))
