import("counter.f")
print(bump())
fn(work()) { return [1, 2].map(bump) }
t := thread(work)
print(t.join())
print(peek())
