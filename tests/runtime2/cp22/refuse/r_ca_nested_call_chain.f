fn(c3(n)) { return sha256("x" + n) }
fn(c2(n)) { return c3(n) }
fn(c1(n)) { return c2(n) }
fn[async](t(n)) { return c1(n) }
print(await t(1))
