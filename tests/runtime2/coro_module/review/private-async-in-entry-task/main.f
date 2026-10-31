print("pre")
import("pa.f")
t := thread(go, 3)
print(t.join())
