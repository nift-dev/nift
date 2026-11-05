// B4 review H1: `substr` on a struct that does not declare it, at top level: legacy runs the
// string native on the instance's tag text in every context; refused before any effect.
struct(S) { a := 1 }
c := S()
print("before")
print(c.substr(1, 1))
