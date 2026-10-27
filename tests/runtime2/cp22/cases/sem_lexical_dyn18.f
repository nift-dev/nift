x := 1
fn[async](t()) { sleep(1); return x }
fn(mid()) { return t() }
fn(h(x)) { k := mid; return k() }
print(await h(9))
