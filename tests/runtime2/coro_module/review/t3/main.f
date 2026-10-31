print("pre")
import("counter.f")
fut := 0
fn[async](w()) { return peek() }
fut = w()
print(await fut)
