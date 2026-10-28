fn[async](mk()) { return (x) => { return x + 1 } }
fn[async](outer()) { f := await mk(); return f(1) }
print(await outer())
