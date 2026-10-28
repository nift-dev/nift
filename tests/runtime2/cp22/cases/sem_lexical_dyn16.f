x := 1
fn[async](t()) { sleep(1); return x }
fn(mid()) { return await t() }
fn(h(x)) { k := mid; return k() }
print(h(9))
