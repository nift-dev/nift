fn[async](asq(x)) { return x * x }
fs := [asq]
print(await fs[0](3))
