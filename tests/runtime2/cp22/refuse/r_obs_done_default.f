fn[async](f(x)) { return x + 1 }
p := f(1)
v := await p
print(p.done())
print(v)
