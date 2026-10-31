print("pre")
import("m.f")
t := thread(sq, 3)
print(t.join())
fn[async](w(x)) { return sq(x) + 1 }
print(await w(4))
