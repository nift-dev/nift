m := mutex(0)
fn(incr()) { m.lock(); v := m.get(); m.set(v + 1); m.unlock(); return 0 }
export(incr)
export(m)
