fn(mk()) { return mutex(0) }
m := mk()
fn(incr(x)) { m.lock(); m.set(m.get() + 1); m.unlock(); return 0 }
fn(total()) { return m.get() }
export(incr)
export(total)
