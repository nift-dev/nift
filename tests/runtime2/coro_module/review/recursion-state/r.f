depth := 0
fn(f(n)) { if (n == 0) { return depth }; return f(n - 1) }
export(f)
