struct(Q) { fn(go()) { return 5 } }
print("pre")
import("sm.f")
fn(work()) { return incr() }
a := thread(work)
b := thread(work)
a.join()
b.join()
m.lock()
print(m.get())
m.unlock()
