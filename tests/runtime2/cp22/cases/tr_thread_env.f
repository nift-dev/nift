fn(probe(n)) { e := getenv("NIFT_CP22_UNSET_T"); return n }
fn(outer(n)) { a := thread(probe, n); b := thread(probe, n + 1); return a.join() + b.join() }
fn[async](task(n)) { t := thread(probe, n); return t.join() }
r := thread(outer, 1)
print(r.join())
print(await task(5))
