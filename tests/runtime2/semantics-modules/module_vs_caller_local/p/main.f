import("priv.nft")
fn(g()) { hidden := 42
return rd() }
print(g())
fn(h()) { return hidden2 }
fn(k()) { hidden2 := 5
return h() }
print(k())
