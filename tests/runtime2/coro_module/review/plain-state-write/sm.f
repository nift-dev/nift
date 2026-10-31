n := 0
fn(incr(x)) { n += 1; return n }
fn(total()) { return n }
export(incr)
export(total)
