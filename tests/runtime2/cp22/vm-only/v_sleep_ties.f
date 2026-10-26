fn[async](f(x)) { sleep(30); print("w" + x); return x }
a := f(1)
b := f(2)
c := f(3)
print(await c)
