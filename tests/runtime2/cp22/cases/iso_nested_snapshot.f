g := 1
fn[async](child()) { return g }
fn[async](parent()) { g = 5; c := child(); g = 7; return [g, await c] }
r := await parent()
print(r[0])
print(r[1])
print(g)
