// B4 review M2: a statement that fails at run time before the colliding import: legacy
// reports that error, not the collision; refused (import-collision).
mf := 1
xs := [1, 2]
print(xs[1/0])
import("m.f")
print(mf)
