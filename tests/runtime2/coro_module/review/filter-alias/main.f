print("pre")
import("sm.f")
reader := (x) => incr() == 0
fn(work()) { return [1].filter(reader).length() }
a := thread(work)
b := thread(work)
a.join()
b.join()
m.lock()
print(m.get())
m.unlock()
