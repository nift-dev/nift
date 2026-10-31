fn[async](asq(x)) { return x * x }
o := {"f": asq}
g := o.f
print(await g(3))
