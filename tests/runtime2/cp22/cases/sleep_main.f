fn[async](f()) { return 7 }
p := f()
sleep(300)
print(p.done())
print(await p)
