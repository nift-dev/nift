import("counter.f")
print(bump(0))
reader := (a) => bump(a)
fn(work(g)) { return [3,1,2].map(g) }
t := thread(work, reader)
print(t.join())
print(peek())
