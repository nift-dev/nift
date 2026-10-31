fn(work(x)) { return x * 2 }
fn(kick(x)) { t := thread(work, x); return t.join() }
r := [21].map(kick)
fn(result()) { return r[0] }
export(result)
