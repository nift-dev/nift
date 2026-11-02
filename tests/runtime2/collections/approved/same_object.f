// B2 (b2-refrep.md; Phase B plan section 3, D-NEXT-refsem-b; coordinator ruling 2026-11-03): objects
// are references, so same() answers their identity (legacy: an error; now an approved difference).
print("ran")
o := {"k": 1}
print(same(o, o))
