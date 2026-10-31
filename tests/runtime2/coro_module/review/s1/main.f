struct(Q) { fn(filter(f)) { return 0 } }
print("pre")
import("counter.f")
print(bump(0))
reader := bump
fn(work()) { r := [1, 2].filter(reader); return r.length() }
t := thread(work)
print(t.join())
print(peek())
