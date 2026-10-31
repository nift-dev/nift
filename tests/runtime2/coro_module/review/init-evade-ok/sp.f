fn(work(x)) { return x * 2 }
fn(kick(x)) { t := thread(work, x); return t.join() }
k := kick
r := [21].map(k)
fn(result()) { return r[0] }
export(result)
