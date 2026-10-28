fn[async](add(a, b)) { return a + b }
g := add
p := g(1, 2)
print(type(p))
print(await p)
