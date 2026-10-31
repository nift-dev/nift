count := 5
fn(bump()) { count += 1; return count }
fn(peek()) { return count }
export(bump)
export(peek)
export(count)
