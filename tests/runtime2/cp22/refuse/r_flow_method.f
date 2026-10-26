fn[async](f(a)) { return a }
p := f(1)
print(p.await())
