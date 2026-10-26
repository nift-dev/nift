fn[async](inner()) { return 3 }
fn[async](outer()) { return inner() }
q := await outer()
print(type(q))
print(await q)
