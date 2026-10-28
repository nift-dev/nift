fn[async](spin(xs)) { i := 0; for (sleep : xs) { sleep(1); i += 1 }; return i }
print(await spin([5, 6]))
