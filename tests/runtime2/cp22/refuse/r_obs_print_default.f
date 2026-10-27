fn[async](f(x)) { print("task " + x); return x + 1 }
p := f(1)
print(await p)
