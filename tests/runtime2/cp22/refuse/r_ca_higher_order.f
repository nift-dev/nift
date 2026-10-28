fn(spin(n)) { i := 0; while (i < n) { i += 1 }; return i }
fn[async](apply(f, x)) { return f(x) }
print(await apply(spin, 1000))
