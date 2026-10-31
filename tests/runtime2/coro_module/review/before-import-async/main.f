fn[async](w()) { return target(3) }
f := w()
import("clean.f")
print(await f)
