print("pre")
import("counter.f")
import("h.f")
fn(w()) { return apply((v) => peek() + v, 4) }
t := thread(w)
print(t.join())
