print("pre")
import("sm.f")
o := [incr]
fn(work()) { return o[0]() }
a := thread(work)
b := thread(work)
a.join()
b.join()
m.lock()
print(m.get())
m.unlock()
