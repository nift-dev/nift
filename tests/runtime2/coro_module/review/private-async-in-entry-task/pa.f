fn[async](asq(x)) { return x * x }
fn(go(a)) { f := asq(a); return await f }
export(go)
