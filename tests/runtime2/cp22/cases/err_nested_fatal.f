fn[async](inner()) { x := [1]; return x[5] }
fn[async](middle()) { return await inner() }
print(await middle())
