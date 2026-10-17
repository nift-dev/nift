@fn[async](inner(n)) { return exists("missing-" + n) }
@fn[async](outer(n)) { a := inner(n); b := inner(n + 1); return (await a) + "," + (await b) }
r := outer(1)
print(await r)
