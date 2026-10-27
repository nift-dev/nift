fn[async](leaf(x)) { return x * 2 }
fn[async](sum(n)) { hs := [leaf(1), leaf(2), leaf(n)]; s := 0; for (h : hs) { s += await h }; return s }
print(await sum(3))
