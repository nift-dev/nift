o := {"f": async (x) => x * x}
v := await o.f(3)
fn(get()) { return v }
export(get)
