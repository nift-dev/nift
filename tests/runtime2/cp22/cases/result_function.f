fn(id(x)) { return x }
fn[async](f()) { return id }
k := await f()
print(k(7))
