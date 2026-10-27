fn[async](poll(n)) { i := 0; while (i < n) { sleep(0); i += 1 }; return i }
print(await poll(10))
