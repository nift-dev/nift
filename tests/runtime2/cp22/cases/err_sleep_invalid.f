fn[async](f(ms)) { sleep(ms); return 1 }
p := f(-1)
print(await p)
