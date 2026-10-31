fn[async](asq(x)) { return x * x }
o := {"f": asq}
fn(go(a)) { fut := o.f(a); return await fut }
export(go)
