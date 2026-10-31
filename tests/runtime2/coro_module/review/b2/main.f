struct(Q) { fn(go(x)) { m.lock(); v := m.get(); m.set(v + 1); m.unlock(); return 0 } }
print("pre")
import("sm.f")
fn(work()) { q := Q(); return q.go(1) }
a := thread(work)
b := thread(work)
a.join()
b.join()
m.lock()
print(m.get())
m.unlock()
