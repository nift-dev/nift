print("pre")
import("m.f")
print(bump())
struct(P) { fn(get()) { return 2 } }
p := P()
fn(work()) { return sq(p.get()) }
t := thread(work)
print(t.join())
