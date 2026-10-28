fn(make()) { n := 0; return () => { n += 1; return n } }
counter := make()
fn[async](tick(c)) { return c() }
a := tick(counter)
b := tick(counter)
print((await a) + (await b))
print(counter())
