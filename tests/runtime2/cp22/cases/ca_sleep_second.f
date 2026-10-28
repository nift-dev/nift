fn[async](spin()) { i := 0; while (i < 3) { i += 1; sleep(1) }; return i }
print(await spin())
