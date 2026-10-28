fn[async](f(x)) { print("t" + x); return x }
a := f(1)
b := f(2)
c := f(3)
print("m")
print(await c)
