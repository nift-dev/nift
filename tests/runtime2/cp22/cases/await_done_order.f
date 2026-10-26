fn[async](f(n)) { sleep(n); return n }
a := f(60)
b := f(10)
sleep(300)
print(await b)
print(await a)
