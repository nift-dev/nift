// B4 re-review N1 (an object holding the literal 1e999 before the import): legacy stops before the collision check;
// refused (import-collision), not the collision error.
mf := 1
x := {"a": 1e999}
import("m.f")
