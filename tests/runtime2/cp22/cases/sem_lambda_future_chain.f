fn[async](mk()) { return (x) => { return x + 10 } }
fn[async](use(p)) { f := await p; return f(1) }
p := mk()
print(await use(p))
q := mk()
fq := await q
print(fq(2))
