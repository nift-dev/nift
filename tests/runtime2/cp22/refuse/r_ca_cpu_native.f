fn[async](digest(s)) { return sha256(s) }
a := digest("abc")
print(await a)
