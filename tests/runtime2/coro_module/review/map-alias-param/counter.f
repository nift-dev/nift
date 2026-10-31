count := 0
fn(bump(x)) { count += 1; return count }
fn(peek()) { return count }
export(bump)
export(peek)
