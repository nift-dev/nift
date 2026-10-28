fn[async](spin(n)) { i := 0; while (i < n) { ms := 1; if (i > 1) { ms = 0 }; sleep(ms); i += 1 }; return i }
print(await spin(20000))
