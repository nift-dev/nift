// B4 re-review N1 (an integer literal outside the signed 64-bit range before the import): legacy stops before the collision check;
// refused (import-collision), not the collision error.
mf := 1
n := 99999999999999999999999999
import("m.f")
print(mf)
