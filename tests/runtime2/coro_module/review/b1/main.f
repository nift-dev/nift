struct(Q) { fn(go(x)) { m.lock(); v := m.get(); m.set(v + 1); m.unlock(); return 0 } }
print("pre")
import("sm.f")
q := Q()
g := q.go
fn(work()) { return g(1) }
a := thread(work)
b := thread(work)
a.join()
b.join()
m.lock()
print(m.get())
m.unlock()
