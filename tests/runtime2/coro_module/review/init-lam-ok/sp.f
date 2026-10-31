fn(work(x)) { return x * 2 }
k := (x) => thread(work, x).join()
r := [21].map(k)
fn(result()) { return r[0] }
export(result)
