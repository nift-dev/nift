print("pre")
import("counter.f")
fn(k(f)) { return (x) => f(x) }
fn(work()) { return k(bump)(1) }
t := thread(work)
print(t.join())
print(peek())
