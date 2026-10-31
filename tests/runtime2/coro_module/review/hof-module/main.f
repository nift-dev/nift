import("clean.f")
import("h.f")
fn(w()) { return apply(target, 4) }
t := thread(w)
print(t.join())
fn[async](aw()) { return apply((v) => v + 1, 4) }
print(await aw())
