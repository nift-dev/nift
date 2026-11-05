// B4 review M2: a top-level `return` before the colliding import: legacy stops there (exit 0)
// and never reaches the import; refused (import-collision), not the collision error.
mf := 1
print("first")
if (true) { return }
import("m.f")
print(mf)
