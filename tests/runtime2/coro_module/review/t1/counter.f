count := 0
fn(bump()) { count += 1; return count }
fn(peek()) { return count }
export(bump)
export(peek)
