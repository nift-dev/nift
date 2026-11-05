// B3 (review N1), B4: the module's xs[0] is not proven; B3 refused this before any effect,
// B4 chooses at run time (StructIs): the value now there is a map, rendered by the built-in.
import("m.f")
print("before")
print(show())
