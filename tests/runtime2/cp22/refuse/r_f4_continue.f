fn[async](spin()) { i := 0; while (i < 3000000) { i += 1; if (i > 0) { continue }; sleep(1) }; return i }
p := spin()
q := spin()
print((await p) + (await q))
