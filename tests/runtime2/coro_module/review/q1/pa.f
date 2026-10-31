o := {"f": async (x) => x * x}
fn(go(a)) { fut := o.f(a); return await fut }
export(go)
