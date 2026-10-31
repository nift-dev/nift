print("pre")
import("counter.f")
fn(rec(n, g)) { if (n == 0) { return g() } return rec(n - 1, g) }
t := thread(rec, 3, bump)
print(t.join())
