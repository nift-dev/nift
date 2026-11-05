// B3 (review H1): a struct declared in an imported module is visible: an
// explicit stringify() on a mixed receiver that may be its instance is refused
// before any effect.
import("m.f")
print("before")
a := make()
xs := [a, map()]
for (x : xs) { print(x.stringify()) }
