print("pre")
k := (x) => later(x)
fn(work()) { r := [1, 2].map(k); return r[0] + r[1] }
t := thread(work)
import("clean.f")
print(t.join())
