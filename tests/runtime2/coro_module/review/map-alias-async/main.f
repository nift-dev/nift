print("pre")
import("counter.f")
print(bump(0))
reader := bump
fn[async](work()) { r := [1, 2].map(reader); return r[0] + r[1] }
print(await work())
print(peek())
