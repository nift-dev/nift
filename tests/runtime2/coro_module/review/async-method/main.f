struct(S) { fn[async](go()) { return peek() } }
print("pre")
import("counter.f")
s := S()
print(await s.go())
