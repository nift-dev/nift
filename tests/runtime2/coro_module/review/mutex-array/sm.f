ms := [mutex(0)]
fn(incr(x)) { m := ms[0]; m.lock(); m.set(m.get() + 1); m.unlock(); return 0 }
fn(total()) { return ms[0].get() }
export(incr)
export(total)
