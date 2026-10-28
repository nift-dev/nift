fn[async](spin()) { i := 0; s := 0; while (i < 3) { sleep(1); j := 0; while (j < 1000) { j += 1 }; s += j; i += 1 }; return s }
print(await spin())
