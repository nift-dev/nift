g := 1
fn(w()) { g = 7; return g }
fn(r()) { return g }
t := thread(w)
g = 3
print(t.join())
print(g)
print(thread(w).join() + 1)
print(thread(r).join())
print(g)
