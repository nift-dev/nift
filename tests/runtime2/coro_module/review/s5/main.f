print("pre")
import("counter.f")
fn(work(a = bump(0))) { return a }
t := thread(work)
print(t.join())
print(peek())
