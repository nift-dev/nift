fn(go(a)) { h := async (x) => x * x; return await h(a) }
export(go)
