fn[async](a()) { return (x) => { return x + 1000 } }
fa := await a()
m := (x) => { return x - 1 }
print(fa(1))
print(m(1))
