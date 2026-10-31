fn[async](asq(x)) { return x * x }
fn(call(p)) { return await p.f(3) }
print(call({"f": asq}))
