fn(sq(x)) { return x * x }
fn(go(a)) { t := thread(sq, a); return t.join() }
export(go)
