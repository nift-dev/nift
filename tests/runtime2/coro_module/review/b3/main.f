struct(Q) { fn(sort(f)) { return 0 } }
print("pre")
import("sm.f")
reader := incr
fn(cmpf(a, b)) { reader(0); return a < b }
fn(work()) { r := [2, 1].sort(cmpf); return r[0] }
a := thread(work)
b := thread(work)
a.join()
b.join()
m.lock()
print(m.get())
m.unlock()
