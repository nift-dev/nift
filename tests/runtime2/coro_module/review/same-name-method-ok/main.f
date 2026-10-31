struct(Q) { fn(map(f)) { return 0 } }
print("pre")
import("m.f")
fn(work()) { r := [1, 2].map(sq); return r[1] }
t := thread(work)
print(t.join())
