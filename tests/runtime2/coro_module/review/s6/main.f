print("pre")
import("counter.f")
fn(work()) { return bump(0) }
try { t := thread(work); print(t.join()) } catch(e) { print("c") }
print(peek())
