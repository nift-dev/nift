g := 100
fn[async](mk()) { return [(x) => { return x + 1 }, (x) => { return x * 2 }] }
fn[async](mko()) { return {"inc": (x) => { return x + 1 }, "glob": () => { return g }} }
fs := await mk()
f0 := fs[0]
f1 := fs[1]
print(f0(1))
print(f1(5))
o := await mko()
print(o.inc(10))
g = 7
print(o.glob())
fn(apply(f, v)) { return f(v) }
print(apply(f1, 21))
