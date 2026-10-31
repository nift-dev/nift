print("pre")
import("cl.f")
g := mk()
fn(w(h)) { return h() }
t := thread(w, g)
print(t.join())
