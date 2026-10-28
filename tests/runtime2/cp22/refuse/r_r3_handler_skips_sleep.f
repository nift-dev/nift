fn[async](spin(n)) { i := 0; while (i < n) { i += 1; try { if (i > 1) { throw error("x") }; sleep(1) } catch (e) { i += 0 } }; return i }
print(await spin(20000))
