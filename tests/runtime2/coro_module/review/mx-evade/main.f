print("pre")
import("sm.f")
reader := incr
fn(work()) { r := [1].map(reader); return r[0] }
a := thread(work)
b := thread(work)
a.join()
b.join()
m.lock()
print(m.get())
m.unlock()
