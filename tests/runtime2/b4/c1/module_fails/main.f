// B4 review M2: the imported module fails before the collision is checked (legacy runs the
// module first): legacy reports the module's error; refused (import-collision).
mf := 1
import("m.f")
print(mf)
