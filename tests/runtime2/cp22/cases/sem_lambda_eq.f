fn[async](a()) { return (x) => { return x + 1 } }
fn[async](b()) { return (x) => { return x * 2 } }
fa := await a()
fb := await b()
print(fa == fb)
