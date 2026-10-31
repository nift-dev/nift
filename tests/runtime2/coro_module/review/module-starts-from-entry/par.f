fn(sq(x)) { return x * x }
fn[async](asq(x)) { return x * x }
fn(go(a)) { t := thread(sq, a); f := asq(a + 1); return t.join() + await f }
export(go)
