g := 1
fn[async](f()) { g = g + 10; return g }
p := f()
g = 100
print(await p)
print(g)
