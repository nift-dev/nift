print("pre")
import("counter.f")
print(bump(0))
reader := bump
fn(work(g)) { r := [1, 2].map(g); return r[0] + r[1] }
t := thread(work, reader)
print(t.join())
print(peek())
