// B4 re-review N1 (the second module declares 9223372036854775808): legacy stops before the collision check;
// refused (import-collision), not the collision error.
mf := 1
import("m0.f")
import("m.f")
