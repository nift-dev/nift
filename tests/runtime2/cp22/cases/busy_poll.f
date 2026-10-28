fn[async](f()) { return 1 }
p := f()
i := 0
while(!p.done()) { i += 1 }
print(await p)
