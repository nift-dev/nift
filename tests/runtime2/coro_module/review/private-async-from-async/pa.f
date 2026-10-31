fn[async](asq(x)) { return x * x }
fn[async](ago(a)) { f := asq(a); return await f }
export(ago)
