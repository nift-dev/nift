fn[async](mk()) { return async (x) => { return x + 1 } }
f := await mk()
print(await f(1))
