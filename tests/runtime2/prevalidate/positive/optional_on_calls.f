fn(f()) { return {"x": 3} }
fn(g()) { return null }
fn(id(v)) { return v }
print(f()?["x"])
print(f()?.x)
print(g()?["x"])
print(id(null?["x"]))
