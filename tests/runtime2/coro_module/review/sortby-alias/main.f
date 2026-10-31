import("counter.f")
print(bump())
reader := (a) => bump()
fn(work(g)) { return [3,1,2].map(g) }
t := thread(work, reader)
print(t.join())
print(peek())
