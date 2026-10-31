print("pre")
import("pa.f")
h := async (x) => go(x)
print(await h(3))
