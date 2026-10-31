print("pre")
import("counter.f")
x := count
fn(w()) { x += 1; return x }
t := thread(w)
print(t.join())
print(x)
print(bump())
