print("pre")
import("counter.f")
fs := [bump]
fn(work()) { g := fs[0]; return g(1) + g(1) }
t := thread(work)
print(t.join())
print(peek())
