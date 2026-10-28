fn[async](leaf(n)) { return n }
fn[async](node(n)) { a := leaf(n); b := leaf(n + 1); return (await a) + (await b) }
fn[async](root()) { hs := []; i := 0; while(i < 10) { hs.push(node(i)); i += 1 }; t := 0; for(h : hs) { t += await h }; return t }
print(await root())
