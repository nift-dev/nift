// B4 C1: a function declared before the import collides the same way.
fn(mf()) { return 1 }
import("m.f")
print("after")
