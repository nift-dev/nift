fn(w()) { sleep(50); return target(3) }
t := thread(w)
import("clean.f")
print(t.join())
