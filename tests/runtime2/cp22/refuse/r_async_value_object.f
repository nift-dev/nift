fn[async](asq(x)) { return x * x }
o := {"f": asq}
print(await o.f(3))
