fn[async](asq(x)) { return x * x }
print("pre")
import("pa.f")
print(go(asq, 3))
