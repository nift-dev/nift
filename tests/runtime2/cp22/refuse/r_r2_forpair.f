id := (n) => { return n }
fn[async](spin(o)) { i := 0; for ((k, sleep) : o) { sleep(1); while (i < 300) { sleep(1); i += 1 } }; return i }
print(await spin({"a": id}))
