fn[async](asq(x)) { return x * x }
fn(go(a)) { fs := [a].map(asq); return await fs[0] }
export(go)
