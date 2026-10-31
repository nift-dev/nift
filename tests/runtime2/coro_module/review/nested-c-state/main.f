print("pre")
import("a.f")
fn(w()) { return a_get() }
t := thread(w)
print(t.join())
