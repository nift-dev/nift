// B2 (b2-refrep.md; Phase B plan section 3, D-NEXT-refsem-b; coordinator ruling 2026-11-03): arrays
// are references, so same() answers their identity, as legacy does (was an approved difference).
print("ran")
a := [1]
b := [1]
print(same(a, b))
