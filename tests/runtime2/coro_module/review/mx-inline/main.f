print("pre")
m := mutex(0)
fn(incr(x)) { m.lock(); v := m.get(); m.set(v + 1); m.unlock(); return 0 }
reader := incr
fn(work()) { r := [1].map(reader); return r[0] }
a := thread(work)
b := thread(work)
a.join()
b.join()
m.lock()
print(m.get())
m.unlock()
