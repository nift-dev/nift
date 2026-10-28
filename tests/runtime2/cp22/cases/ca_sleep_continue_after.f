fn[async](spin(n)) { i := 0; s := 0; while (i < n) { sleep(1); i += 1; if (i % 2 == 0) { continue }; s += i }; return s }
print(await spin(6))
