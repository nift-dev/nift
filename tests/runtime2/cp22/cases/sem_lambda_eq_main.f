fn[async](a()) { return (x) => { return x + 1 } }
fa := await a()
m := (x) => { return x - 1 }
print(fa == m)
