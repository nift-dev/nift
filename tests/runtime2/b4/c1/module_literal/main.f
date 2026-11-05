// B4 re-review N1 (the module declares the literal 1e999 first): legacy stops before the collision check;
// refused (import-collision), not the collision error.
mf := 1
import("m.f")
