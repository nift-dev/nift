print("pre")
import("sm.f")
o := {"go": incr}
fn(work()) { return o.go() }
a := thread(work)
b := thread(work)
a.join()
b.join()
m.lock()
print(m.get())
m.unlock()
