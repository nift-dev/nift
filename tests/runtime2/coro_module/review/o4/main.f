print("pre")
import("m.f")
h := bump
print(h())
fn(work()) { r := [1, 2].map(sq); return r[1] }
t := thread(work)
print(t.join())
