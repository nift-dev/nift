fn[async](mk()) { return (x) => { return x + 1 } }
fn[async](use(p)) { f := await p; return f(1) }
print(await use(mk()))
