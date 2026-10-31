print("pre")
import("m.f")
fn(work(a)) { return a.length() + sq(2) }
t := thread(work, "abc")
print(t.join())
