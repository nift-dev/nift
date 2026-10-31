fn[async](asq(x)) { return x * x }
struct(S) { fn(run(a)) { fut := asq(a); return await fut } }
fn(go(a)) { s := S(); return s.run(a) }
export(go)
