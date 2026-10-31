count := 0
fn(bump()) { count += 1; return count }
fn(sq(x)) { return x * x }
export(bump)
export(sq)
