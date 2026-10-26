fn[async](f(x)) { print("in " + x); return x }
a := f(1)
print(await a)
b := f(2)
print(await b)
