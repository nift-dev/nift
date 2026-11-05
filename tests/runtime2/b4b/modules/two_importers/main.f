// B4b C6: a module imported by two importers; one stores into its container, the
// other's function reads it.
import("a.f")
import("b.f")
print("before")
print(show())
