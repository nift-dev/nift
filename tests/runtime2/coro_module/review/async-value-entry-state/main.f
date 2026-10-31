print("pre")
import("counter.f")
fn[async](rd()) { return peek() }
o := {"f": rd}
print(await o.f())
