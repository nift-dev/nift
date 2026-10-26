fn[async](f(n)) { sleep(n); return n }
a := f(60)
b := f(10)
c := f(30)
print(await b)
print(await c)
print(await a)
