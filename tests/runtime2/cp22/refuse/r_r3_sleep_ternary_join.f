fn[async](spin(n)) { i := 0; while (i < n) { sleep(i > 1 ? 0 : 1); i += 1 }; return i }
print(await spin(20000))
