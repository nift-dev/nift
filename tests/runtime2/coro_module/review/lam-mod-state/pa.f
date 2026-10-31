count := 4
fn(go(a)) { h := async (x) => x * count; return await h(a) }
export(go)
