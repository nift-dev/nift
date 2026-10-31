fn[async](asq(x)) { return x * x }
o := {"f": asq}
v := await o.f(3)
fn(get()) { return v }
export(get)
