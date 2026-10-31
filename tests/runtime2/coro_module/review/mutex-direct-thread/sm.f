m := mutex(0)
fn(incr(x)) { m.lock(); m.set(m.get() + 1); out := 0 + m.get(); m.unlock(); return out }
export(incr)
