fn[async](f(n)) { return n }
x := f(1)
x = f(2)
print(await x)
y := 5
y = f(3)
