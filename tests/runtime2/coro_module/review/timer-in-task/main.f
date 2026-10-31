import("clean.f")
fn(w()) { sleep(10); return target(1) }
t := thread(w)
print(t.join())
