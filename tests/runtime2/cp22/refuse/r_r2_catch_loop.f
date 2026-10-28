fn[async](spin()) { i := 0; try { throw error("x") } catch (sleep) { while (i < 300) { sleep(1); i += 1 } }; return i }
print(await spin())
