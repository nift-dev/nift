print("pre")
import("r.f")
t := thread(f, 3)
print(t.join())
