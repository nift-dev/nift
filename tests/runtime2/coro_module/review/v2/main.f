fn[async](asq(x)) { return x * x }
fn(id(g)) { return g }
print(await id(asq)(3))
