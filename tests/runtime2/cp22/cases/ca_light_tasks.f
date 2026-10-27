fn(scale(x)) { return x * 10 }
fn[async](leaf(x)) { sleep(1); return scale(x) + 1 }
fn[async](pair(x)) { a := leaf(x); b := leaf(x + 1); return (await a) + (await b) }
hs := []
i := 0
while (i < 5) { hs.push(pair(i)); i += 1 }
s := 0
for (h : hs) { s += await h }
print(s)
print(type(hs[0]))
print(hs[0].done())
