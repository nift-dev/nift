// B3 (review H1), B4: a struct declared in an imported module is visible: an
// explicit stringify() on a mixed receiver that may be its instance runs that
// struct's own method for the instance (StructIs; the instance comes from the
// module) and the built-in for the map. B3 refused it before any effect.
import("m.f")
print("before")
a := make()
xs := [a, map()]
for (x : xs) { print(x.stringify()) }
