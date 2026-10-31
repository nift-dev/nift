print("pre")
import("pa.f")
h := async (x) => x * 2
print(go(h, 3))
