print("pre")
m := mutex(0)
fn(incr(x)) { m.lock(); m.set(m.get() + 1); out := 0 + m.get(); m.unlock(); return out }
reader := incr
fn(work()) { r := [1].map(reader); return r[0] }
a := thread(work)
b := thread(work)
x := a.join()
y := b.join()
print(x + y)
print(incr(0))
