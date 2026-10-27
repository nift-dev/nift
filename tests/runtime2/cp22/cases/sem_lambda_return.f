fn[async](mk()) { return (x) => { return x + 1 } }
f := await mk()
print(f(1))
