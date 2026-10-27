fn[async](probe(p)) { return exists(p) }
a := probe("missing-file")
print(await a)
