fn[async](asq(x)) { return x * x }
fn(go(a)) { h := asq; f := h(a); return await f }
export(go)
