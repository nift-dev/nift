fn[async](spin(n, ms)) { i := 0; while (i < n) { sleep(ms); i += 1 }; return i }
print(await spin(20000, 0))
