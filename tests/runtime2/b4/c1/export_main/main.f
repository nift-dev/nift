// B4 re-review N1 (a main-file export naming no binding before the import): legacy stops before the collision check;
// refused (import-collision), not the collision error.
mf := 1
export(nosuch)
import("m.f")
