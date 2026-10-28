fn[async](spin()) { i := 0; while (i < 30) { i += 1; if (i % 2 == 0) { sleep(1) } }; return i }
print(await spin())
