fn[async](leaf(n)) { return n }
fn[async](node(n)) { a := leaf(n); b := leaf(n + 1); return (await a) + (await b) }
fn[async](root()) { hs := []; i := 0; while(i < 10) { sleep(1); hs.push(node(i)); i += 1 }; t := 0; t += await hs[0]; t += await hs[9]; return t }
print(await root())
