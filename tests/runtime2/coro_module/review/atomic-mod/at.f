a := atomic(0)
fn(inc()) { a.add(1); return 0 }
export(inc)
export(a)
