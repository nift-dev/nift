fn[async](one()) { return 1 }
fn[async](two()) { return 2 }
fn[async](five()) { return 5 }
fn[async](yes()) { return true }
await one()
print("ok")
print(!await yes())
print((await one()) + 1)
print(await (five()))
x := await two()
print(x)
