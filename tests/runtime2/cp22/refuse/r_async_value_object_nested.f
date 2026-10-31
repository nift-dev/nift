fn[async](asq(x)) { return x * x }
o := {"in": {"f": asq}}
print(await o.in.f(3))
