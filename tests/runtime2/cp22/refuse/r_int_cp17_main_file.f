fn[async](t(n)) { sleep(1); return n * 2 }
a := t(3)
g := file("w.txt")
g.open("w")
g.write("x")
g.save()
print(await a)
g.close()
