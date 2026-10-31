import("m1.f")
fn[async](go(x)) { return mod_boom(x) }
hs := []
n := 0
while (n < 20) { hs.push(go(n)); n += 1 }
n = 0
while (n < 19) { try { await hs[n] } catch(e) { print(e.message) }; n += 1 }
print(await hs[19])
