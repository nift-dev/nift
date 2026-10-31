print("pre")
import("sm.f")
reader := incr
fn(work()) { r := [1].map(reader); return r[0] }
a := thread(work)
b := thread(work)
x := a.join()
y := b.join()
print(x + y)
print(incr(0))
